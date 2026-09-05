# SimpleORB

一款基于发布/订阅模式的轻量级异步消息中间件，面向嵌入式裸机与 RTOS 环境。

## 特色

- **发布/订阅模型**：以主题（Topic）为单位解耦模块间通信，一个主题支持多个订阅者
- **双临界区保护方案**：
  - SequenceLock：适合裸机环境或写少读多场景，订阅者无锁读取
  - Mutex：适合 RTOS 环境且读写均衡场景
- **阻塞 / 非阻塞订阅**：支持邮箱回调的事件驱动（阻塞等待），也支持纯轮询
- **零动态内存**：句柄与数据缓冲区均由用户静态提供
- **多种定位方式**：支持通过句柄、主题名、UUID（FNV-1a 哈希）操作主题
- **纯 C11 实现**：仅依赖 `stdbool.h`、`stdint.h`、`stdatomic.h`，易于移植

## 目录结构

```
Inc/            库头文件（simple_orb.h）
Src/            库源文件（simple_orb.c）
_Example/Bare/  裸机示例（STM32H5 Nucleo，SequenceLock 方案）
_Example/RTOS/  FreeRTOS 示例（STM32H5 Nucleo，Mutex 方案）
```

## 快速上手

将 `Inc/simple_orb.h` 与 `Src/simple_orb.c` 加入工程，然后按以下步骤使用。

### 1. 实现临界区保护函数

创建与订阅操作需要保护内部链表，由用户根据平台实现：

```c
void ORBCriticalEnter(void) { taskENTER_CRITICAL(); }  // 裸机可用 __disable_irq()
void ORBCriticalExit(void)  { taskEXIT_CRITICAL(); }
```

### 2. 创建主题

二选一，不可混用：

```c
static uint32_t buffer;
static ORB_HANDLE_T orbHandle;

// SequenceLock 方案：wait 用于写中态空转降耗，可为 NULL；
// 单发布者时 take/give 传 NULL 即可
ORBCreateUseSequenceLock("timestamp", &orbHandle, wait, NULL, NULL,
                         &buffer, sizeof(buffer));

// Mutex 方案：take/give 必须提供
ORBCreateUseMutex("timestamp", &orbHandle, MutexTake, MutexGive,
                  &buffer, sizeof(buffer));
```

### 3. 发布数据

```c
uint32_t data = 123;
ORBPublishByName("timestamp", &data, sizeof(data));
// 也可用 ORBPublishByHandle / ORBPublishByUUID
```

### 4. 订阅并接收数据

```c
static ORB_SUBSCRIPTION_HANDLE_T subHandle;

// 非阻塞（轮询）模式：send/receive 传 NULL
ORBSubscribeByName("timestamp", &subHandle, NULL, NULL);

bool updated;
ORBCheckNoBlock(&subHandle, &updated);
if (updated) {
    uint32_t value;
    ORBCopy(&subHandle, &value, sizeof(value));
}
```

阻塞（事件驱动）模式：注册时提供 `send`（发布者侧通知）与 `receive`（订阅者侧等待）回调，例如用 RTOS 的线程标志或信号量实现，然后使用 `ORBCheckBlock()` 等待更新。

## 示例

- `_Example/Bare`：按键中断发布时间戳，主循环轮询订阅并通过串口打印
- `_Example/RTOS`：按键发布时间戳，两个订阅任务分别以阻塞方式接收（一个控制 LED，一个串口输出）

## 许可证

[GNU AGPL v3](LICENSE)
