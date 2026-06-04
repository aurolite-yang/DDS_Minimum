# Fast DDS minimal publish-subscribe training

这是一个面向入门学习的最小 Fast DDS C++ 示例，重点只看两件事：

- `DataReaderListener::on_data_available`：异步事件驱动。DDS 内部线程发现数据后主动回调你的代码。
- `WaitSet::wait`：阻塞等待。业务线程主动睡眠，直到 DDS 状态条件被触发后再处理数据。

消息类型在 [idl/HelloWorld.idl](/Users/yuhao/Documents/New%20project%203/idl/HelloWorld.idl) 中定义，`fastddsgen` 会在构建时生成序列化和类型支持代码。

## 1. 准备依赖

需要本机已有这些工具和库：

- CMake
- C++17 编译器
- Fast DDS
- Fast CDR
- Fast DDS-Gen，也就是 `fastddsgen`
- pytest

当前这台机器已经有 CMake，但没有检测到 Fast DDS、Fast CDR、`fastddsgen` 和 pytest，所以本机暂时不能完成真实 DDS 运行。你在公司开发机或装好 Fast DDS Suite 后可以直接用下面命令。

## 2. 编译

```bash
cmake -S . -B build
cmake --build build
```

如果 CMake 报 `fastddsgen was not found`，说明还没安装 Fast DDS-Gen，或者它不在 `PATH` 里。

## 3. 运行 DataReaderListener 模式

终端 1：

```bash
./build/dds_demo sub-listener --samples 5
```

终端 2：

```bash
./build/dds_demo pub --samples 5 --period-ms 200
```

你会在订阅端看到：

```text
LISTENER_SUB index=1 message="hello from Fast DDS"
LISTENER_SUB index=2 message="hello from Fast DDS"
...
```

关键代码在 [src/dds_demo.cpp](/Users/yuhao/Documents/New%20project%203/src/dds_demo.cpp) 的 `ListenerSubscriber::on_data_available`。

## 4. 运行 WaitSet 模式

终端 1：

```bash
./build/dds_demo sub-waitset --samples 5
```

终端 2：

```bash
./build/dds_demo pub --samples 5 --period-ms 200
```

订阅端会阻塞在 `wait_set.wait(...)`。当 DDS 的 `data_available` 状态触发后，代码再调用 `take_next_sample` 取数据。

关键代码在 [src/dds_demo.cpp](/Users/yuhao/Documents/New%20project%203/src/dds_demo.cpp) 的 `run_waitset_subscriber`。

## 5. pytest 自动化测试

安装 pytest 后运行：

```bash
python3 -m pytest -q
```

测试逻辑在 [tests/test_dds_demo.py](/Users/yuhao/Documents/New%20project%203/tests/test_dds_demo.py)：

- 先启动订阅者进程。
- 再启动发布者进程。
- 等两个进程退出。
- 断言发布者发到了 `index=3`，订阅者收到了 `index=3`。

这就是自动驾驶中常见通信测试的最小形态：用测试框架编排多个进程，然后验证进程间通信结果。

## 6. 学习要点

`DataReaderListener` 适合“数据来了立刻反应”的模式。业务代码被 DDS 回调触发，注意回调里不要做太重的工作，否则可能影响 DDS 接收线程。

`WaitSet` 适合“一个线程明确控制等待和处理节奏”的模式。它更像 C 里熟悉的 `select/poll` 思路，实时任务里常用于集中等待多个条件。

两种模式最终都要调用 `take_next_sample` 从 DDS reader 的缓存里取数据。区别不是“有没有缓存”，而是“谁来唤醒业务逻辑”：Listener 是 DDS 回调你，WaitSet 是你的线程阻塞等待 DDS 条件。
