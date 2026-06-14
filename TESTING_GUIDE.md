# DDS Demo 测试说明

本文档说明 `tests/test_dds_demo.py` 的测试目的、执行流程、关键代码和当前 Allure 结果。

## 1. 测试目标

当前测试是一个端到端集成测试，不是普通的 Python 单元测试。

它会启动编译后的 `dds_demo` 可执行文件，并验证：

- publisher 能正常发布 DDS 样本。
- `sub-listener` 模式能收到消息。
- `sub-waitset` 模式能收到消息。
- 发布端和订阅端进程都能正常退出。

测试文件：

```text
tests/test_dds_demo.py
```

默认测试二进制：

```text
build/dds_demo
```

## 2. 前置条件

运行测试前需要先完成构建：

```bash
cmake -S . -B build
cmake --build build
```

如果本机 Fast DDS 环境需要手动加载，先执行：

```bash
source /Users/yuhao/Documents/Codex/FastDDS/install/setup_fastdds.zsh
```

如果 `build/dds_demo` 不存在，测试脚本会自动跳过测试，而不是直接失败。

相关代码：

```python
def demo_binary():
    path = pathlib.Path(os.environ.get("DDS_DEMO_BIN", DEFAULT_BIN))
    if not path.exists():
        pytest.skip(
            "dds_demo binary not found. Build first with: "
            "cmake -S . -B build && cmake --build build"
        )
    return path
```

也可以通过环境变量指定其他二进制路径：

```bash
DDS_DEMO_BIN=/path/to/dds_demo python3 -m pytest
```

## 3. 测试执行流程

核心函数是：

```python
run_pub_sub_pair(mode)
```

它的流程如下：

1. 找到 `dds_demo` 可执行文件。
2. 启动一个 subscriber 子进程。
3. 等待 1 秒，让 subscriber 先完成初始化。
4. 启动 publisher 子进程，发布 3 条样本。
5. 等待 subscriber 退出，并收集 subscriber 输出。
6. 返回 publisher 和 subscriber 的退出码与输出内容。

对应代码：

```python
subscriber = subprocess.Popen(
    [str(binary), mode, "--samples", samples],
    cwd=ROOT,
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
    text=True,
)
```

这里使用 `Popen` 是因为 subscriber 要先启动并保持运行，等待 publisher 发消息。

publisher 使用：

```python
publisher = subprocess.run(
    [str(binary), "pub", "--samples", samples, "--period-ms", "100"],
    cwd=ROOT,
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
    text=True,
    timeout=20,
    check=False,
)
```

这里使用 `subprocess.run` 是因为 publisher 只需要发布固定数量的消息，然后退出。

## 4. `communicate(timeout=20)` 的作用

这行代码：

```python
sub_output, _ = subscriber.communicate(timeout=20)
```

作用是等待 subscriber 进程结束，并读取它的完整输出。

返回值是：

```python
(stdout, stderr)
```

因为启动 subscriber 时设置了：

```python
stderr=subprocess.STDOUT
```

所以标准错误已经合并进标准输出，第二个返回值通常是 `None`。因此代码用 `_` 表示不关心第二个返回值。

如果 subscriber 在 20 秒内没有退出，`communicate` 会抛出超时异常。脚本中的 `finally` 会负责终止 subscriber，避免测试进程残留：

```python
finally:
    if subscriber.poll() is None:
        subscriber.terminate()
        subscriber.wait(timeout=5)
```

## 5. 参数化测试

测试函数通过 `pytest.mark.parametrize` 跑两组用例：

```python
@pytest.mark.parametrize(
    ("mode", "marker"),
    [
        ("sub-listener", "LISTENER_SUB index=3"),
        ("sub-waitset", "WAITSET_SUB index=6"),
    ],
)
def test_pub_sub_receives_three_samples(mode, marker):
    ...
```

两种 subscriber 模式分别是：

- `sub-listener`：基于 `DataReaderListener::on_data_available` 的回调式接收。
- `sub-waitset`：基于 `WaitSet::wait` 的阻塞等待式接收。

每组测试都会启动：

```bash
./build/dds_demo <mode> --samples 3
./build/dds_demo pub --samples 3 --period-ms 100
```

## 6. 断言逻辑

测试最后检查 4 件事：

```python
assert pub_code == 0, pub_output
assert sub_code == 0, sub_output
assert "PUB index=3" in pub_output
assert marker in sub_output
```

含义分别是：

- publisher 进程退出码为 0。
- subscriber 进程退出码为 0。
- publisher 输出中包含 `PUB index=3`，说明确实发布到了第 3 条样本。
- subscriber 输出中包含对应模式的 marker，说明订阅端收到了预期样本。

## 7. 当前 Allure 结果解读

当前 `allure-results` 中有两条测试结果：

```text
test_pub_sub_receives_three_samples[sub-listener-LISTENER_SUB index=3]
test_pub_sub_receives_three_samples[sub-waitset-WAITSET_SUB index=6]
```

其中：

- `sub-listener` 用例通过。
- `sub-waitset` 用例失败。

失败原因是测试期望：

```text
WAITSET_SUB index=6
```

但 subscriber 实际输出是：

```text
WAITSET_SUB index=1 message="hello from Fast DDS"
WAITSET_SUB index=2 message="hello from Fast DDS"
WAITSET_SUB index=3 message="hello from Fast DDS"
```

也就是说，当前测试只发布了 3 条样本：

```python
samples = "3"
```

因此 `sub-waitset` 不可能收到 `index=6`。

## 8. 建议修正

如果测试目标是验证两种 subscriber 都收到 3 条样本，那么参数化里的 waitset marker 应该改成：

```python
("sub-waitset", "WAITSET_SUB index=3")
```

也就是：

```python
[
    ("sub-listener", "LISTENER_SUB index=3"),
    ("sub-waitset", "WAITSET_SUB index=3"),
]
```

这样测试名称 `test_pub_sub_receives_three_samples`、`samples = "3"` 和断言目标就是一致的。

## 9. 常用命令

运行 pytest：

```bash
python3 -m pytest -q
```

生成 Allure 原始结果：

```bash
python3 -m pytest -q --alluredir=allure-results
```

启动 Allure 临时报告：

```bash
allure serve allure-results
```

生成静态 Allure 报告：

```bash
allure generate allure-results -o allure-report --clean
allure open allure-report
```
