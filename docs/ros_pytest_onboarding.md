# ROS communication middleware and Pytest onboarding guide

本文档面向已有 C/Python MCU 端测试开发经验、但刚开始接触 C++、智能驾驶传感器集成、ROS 通信中间件和 Pytest 的测试工程师。

目标是帮助你把已有的 CANoe/ECU 测试经验迁移到 ROS 通信测试中，并能快速理解新业务中的测试对象、测试方法和常见问题。

## 1. 从 MCU/CANoe 测试迁移到 ROS/Pytest 测试

你过去熟悉的测试模型通常是：

```text
MCU ECU
  <-> CAN / LIN / SOME/IP / UDS
  <-> CANoe 仿真节点 / 测试脚本
```

新业务中的测试模型更接近：

```text
传感器 / 感知模块 / 融合模块
  <-> ROS Topic / Service / Action / DDS
  <-> C++ 节点 / Python 测试进程 / Pytest
```

关键迁移关系：

| MCU/CANoe 场景 | ROS/Pytest 场景 |
| --- | --- |
| CANoe 仿真对端 | Pytest 启动 ROS 节点、发布或订阅消息 |
| CAN 报文 / 信号 | ROS message / topic |
| CAPL / Python 测试脚本 | Pytest + rclpy / rosbag / subprocess |
| ECU 接口测试 | 节点通信、数据流、时序、QoS、融合链路测试 |
| 硬件总线 | DDS / ROS middleware |
| 信号值断言 | topic 内容、频率、延迟、坐标系、时间戳断言 |

如果你理解 CANoe 中“仿真一个节点、发送报文、接收报文、检查信号”的思路，那么 ROS + Pytest 的本质也类似：

```text
测试框架编排多个进程，然后验证进程间通信结果。
```

## 2. 你需要优先掌握的 ROS 概念

### 2.1 Topic

Topic 是最重要的通信方式。摄像头、激光雷达、毫米波雷达、定位、感知结果和融合结果，通常都通过 topic 发布。

常见 topic 示例：

```text
/camera/front/image_raw
/lidar/points
/radar/objects
/perception/objects
/vehicle/ego_pose
/tf
/tf_static
```

测试时要优先确认：

- topic 名称是否正确。
- topic 是否真的存在。
- 发布频率是否符合预期。
- message 类型是否符合接口定义。
- publisher 和 subscriber 的 QoS 是否匹配。

### 2.2 Publisher 和 Subscriber

Publisher 负责发布消息，Subscriber 负责订阅消息。

在 CANoe 测试中，你可能会模拟一个 ECU 发送 CAN 报文；在 ROS 测试中，你可以用 Python 测试节点模拟 publisher，向被测 C++ 节点发布输入消息。

反过来，你也可以让 Pytest 创建 subscriber，监听被测节点输出的 topic，然后检查输出内容。

### 2.3 Message

Message 类似 CAN 信号定义，但结构更复杂。自动驾驶传感器和感知链路中常见类型包括：

```text
sensor_msgs/msg/Image
sensor_msgs/msg/PointCloud2
nav_msgs/msg/Odometry
geometry_msgs/msg/PoseStamped
vision_msgs/msg/Detection3DArray
```

测试时不能只看“有没有消息”，还要看：

- `header.stamp` 是否合理。
- `header.frame_id` 是否正确。
- 关键字段是否在合法范围。
- 数组长度、对象数量是否符合预期。
- 时间戳是否单调递增。
- 坐标值是否在合理范围。

### 2.4 QoS

ROS2 底层使用 DDS，QoS 非常关键。QoS 不匹配时，即使 topic 名称完全正确，subscriber 也可能收不到数据。

重点关注：

```text
reliability: reliable / best_effort
durability: volatile / transient_local
history: keep_last
depth: queue size
```

经验规则：

- 传感器原始数据常用 `best_effort`。
- 状态、控制、关键结果类数据更常用 `reliable`。
- 测试脚本中的 QoS 要尽量和业务节点保持一致。

### 2.5 TF 和坐标系

智能驾驶传感器集成中，TF 很重要。它描述不同坐标系之间的关系。

常见 frame：

```text
base_link
camera_front
lidar_top
radar_front
map
odom
```

很多问题不是“消息没发”，而是：

- `frame_id` 不对。
- TF 链路缺失。
- 外参配置错误。
- 时间戳对不上，导致查询不到对应时刻的 TF。

### 2.6 Rosbag

Rosbag 类似“录制下来的总线数据”。可以用来录制、回放真实道路数据或仿真数据。

常用命令：

```bash
ros2 bag play xxx
ros2 bag record /topic1 /topic2
```

测试中常见用法：

```text
播放固定 rosbag
  -> 启动被测节点
  -> 订阅输出 topic
  -> 检查关键场景输出
```

## 3. 阅读 C++ ROS 业务代码时先看什么

刚开始不需要立刻深入算法实现，先看通信边界。

拿到一个 C++ ROS 节点，优先搜索这些接口：

```cpp
create_publisher
create_subscription
create_timer
declare_parameter
get_parameter
```

例如：

```cpp
publisher_ = this->create_publisher<sensor_msgs::msg::PointCloud2>(
    "/lidar/points",
    qos
);
```

你要先回答这些问题：

- 这个节点订阅哪些 topic？
- 这个节点发布哪些 topic？
- 每个 topic 的 message 类型是什么？
- 发布频率是多少？
- QoS 是什么？
- 时间戳来自哪里？
- `frame_id` 是什么？
- 参数从哪里配置？
- 节点启动后是否依赖 TF？
- 是否依赖其他节点先启动？

建议把数据流画出来：

```text
camera driver
  -> /camera/front/image_raw
  -> perception node
  -> /perception/objects
  -> fusion node
  -> /fusion/tracks
```

## 4. Pytest 在 ROS 测试中的作用

Pytest 不是 ROS 专用框架，它是测试编排框架。

它通常负责：

- 启动被测进程。
- 启动辅助节点。
- 发布测试输入。
- 订阅输出 topic。
- 等待结果。
- 执行断言。
- 生成报告，例如 Allure。

当前仓库的 `tests/test_dds_demo.py` 就是一个最小通信测试：

```text
pytest
  -> 先启动 subscriber 进程
  -> 再启动 publisher 进程
  -> 等待两个进程结束
  -> 检查 stdout 中是否有预期结果
```

在真实 ROS 项目中，测试结构通常会变成：

```text
pytest
  -> 启动 ROS 节点
  -> 发布传感器消息或播放 rosbag
  -> 订阅输出 topic
  -> 检查输出内容、频率、时延、数量、字段合法性
```

## 5. 推荐的测试分层

### 5.1 消息接口测试

验证节点是否发布或订阅正确 topic，消息类型、QoS、`frame_id`、timestamp 是否正确。

示例：

```text
启动 perception node
发布一帧 Image
断言 /perception/objects 收到 DetectionArray
```

### 5.2 通信链路测试

验证多个节点串起来后数据能正常流通。

示例：

```text
camera input
  -> perception
  -> fusion
  -> planning input
```

常见断言：

- 输出 topic 是否产生。
- 输出频率是否稳定。
- 是否丢帧。
- latency 是否小于阈值。

### 5.3 数据语义测试

验证输出内容是否合理。

以雷达目标为例：

```text
object position x/y/z 合法
velocity 合法
confidence 在 0 到 1 之间
timestamp 单调递增
frame_id 正确
```

### 5.4 场景回放测试

使用 rosbag 或仿真数据回放固定场景。

示例断言：

```text
前车目标持续存在
检测结果数量在合理范围
轨迹 ID 不频繁跳变
关键时刻输出满足预期
```

## 6. Pytest 测试脚本常见结构

一个 ROS 通信测试通常长这样：

```python
def test_node_publishes_objects():
    # 1. 启动被测节点
    proc = subprocess.Popen([...])

    # 2. 初始化 ROS 测试节点
    # 3. 创建 publisher，模拟输入 topic
    # 4. 创建 subscriber，监听输出 topic
    # 5. 发布测试消息
    # 6. 等待输出
    # 7. assert 检查结果
    # 8. 清理进程
```

对应你过去熟悉的 CANoe 测试模型：

```text
发送 CAN frame
  -> 等待 ECU 响应
  -> 检查 signal
```

在 ROS/Pytest 中对应：

```text
发布 ROS message
  -> 等待输出 topic
  -> 检查 message field
```

## 7. 常用命令清单

ROS2 常用检查命令：

```bash
ros2 topic list
ros2 topic echo /topic_name
ros2 topic info /topic_name
ros2 topic hz /topic_name
ros2 interface show sensor_msgs/msg/Image
ros2 node list
ros2 node info /node_name
ros2 bag play xxx
ros2 bag record /topic1 /topic2
```

常见构建命令：

```bash
colcon build
source install/setup.bash
```

常见测试命令：

```bash
python3 -m pytest
```

或在 ROS 包内使用：

```bash
colcon test
colcon test-result --verbose
```

Allure 报告：

```bash
python3 -m pytest --alluredir=allure-results
allure serve allure-results
```

## 8. 建议学习路线

### 第一阶段：快速建立业务地图

优先完成三件事：

1. 画出节点通信图。
2. 跑通现有构建和测试。
3. 修改一个最小测试，确认自己知道测试为什么通过、为什么失败。

节点通信图至少包含：

```text
node name
input topics
output topics
message type
QoS
frame_id
frequency
parameters
```

### 第二阶段：掌握通信测试能力

重点练习：

- 用 Pytest 启动和清理进程。
- 用 Python 发布 ROS 输入消息。
- 用 Python 订阅 ROS 输出消息。
- 用 timeout 等待预期结果。
- 用 assert 检查 message 字段。
- 用 rosbag 做固定输入源。

### 第三阶段：深入智能驾驶传感器语义

按业务优先级学习：

- 摄像头：Image、CameraInfo、检测框、语义分割结果。
- 激光雷达：PointCloud2、点云坐标系、点云时间戳。
- 毫米波雷达：目标列表、速度、距离、置信度。
- 融合：object track、track id、生命周期、坐标转换。
- TF：坐标系树、外参、时间同步。

## 9. 常见坑位

### 9.1 QoS 不匹配

现象：

```text
ros2 topic list 能看到 topic，但测试收不到消息。
```

排查：

```bash
ros2 topic info /topic_name -v
```

重点看 publisher 和 subscriber 的 reliability、durability、depth 是否兼容。

### 9.2 环境没有 source

现象：

```text
找不到包
找不到节点
找不到 message
```

处理：

```bash
source install/setup.bash
```

如果依赖外部 ROS 环境，还需要先 source 系统或上游 workspace。

### 9.3 进程没有清理

Pytest 失败后，如果 ROS 节点残留，可能影响下一次测试。

建议测试中使用 `try/finally`：

```python
proc = subprocess.Popen([...])
try:
    ...
finally:
    if proc.poll() is None:
        proc.terminate()
        proc.wait(timeout=5)
```

### 9.4 过度依赖 sleep

简单写法：

```python
time.sleep(5)
```

更稳的方式：

```text
等待某个 topic 收到满足条件的数据，并设置 timeout。
```

### 9.5 时间戳问题

自动驾驶链路中很多模块依赖 timestamp。

常见问题：

- 输入消息时间戳为 0。
- 多传感器时间戳不一致。
- 使用仿真时间但没有设置 `/clock`。
- rosbag 回放时没有启用或正确使用 sim time。

### 9.6 frame_id 和 TF 问题

消息有了不代表链路正确。还要确认：

- `header.frame_id` 是否符合接口约定。
- TF 是否存在。
- TF 时间是否能查到。
- 外参是否正确。

## 10. 面向测试开发的工作检查表

接手一个新模块时，建议按下面顺序过一遍：

```text
1. 模块启动命令是什么？
2. 需要哪些参数文件？
3. 输入 topic 有哪些？
4. 输出 topic 有哪些？
5. 每个 topic 的 message type 是什么？
6. QoS 是什么？
7. 输入数据来源是什么，在线传感器还是 rosbag？
8. 输出正确性的判断标准是什么？
9. 是否依赖 TF？
10. 是否依赖仿真时间？
11. 是否需要多个节点按顺序启动？
12. 失败时看哪些日志？
13. 测试报告怎么生成？
```

## 11. 你当前应该建立的能力模型

你的 MCU/CANoe 测试经验可以这样迁移：

```text
总线信号理解能力
  -> ROS topic/message 理解能力

仿真对端能力
  -> 用 Python/Pytest 模拟 publisher/subscriber

ECU 响应断言
  -> ROS message 字段断言

报文时序分析
  -> topic 频率、latency、timestamp 分析

测试自动化经验
  -> Pytest fixture、参数化、报告、CI 集成
```

上手初期不要先陷入所有 C++ 实现细节。更高效的顺序是先围绕通信测试建模：

```text
输入是什么？
谁发布？
谁订阅？
消息类型是什么？
频率是多少？
时间戳和坐标系是什么？
输出如何判断正确？
```

这些问题回答清楚之后，再深入 C++ 节点内部逻辑、传感器语义和算法细节，会更容易形成系统理解。
