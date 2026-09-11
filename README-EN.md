# SimpleORB

中文说明[在此](README.md)。

A lightweight asynchronous message middleware based on the publish/subscribe pattern, targeting embedded bare-metal and RTOS environments.

## Features

- **Publish/subscribe model**: decouples inter-module communication around topics (Topics); a single topic supports multiple subscribers
- **Two critical-section protection schemes**:
  - SequenceLock: suited to bare-metal systems or read-mostly workloads; subscribers read lock-free
  - Mutex: suited to RTOS environments with balanced read/write traffic
- **Blocking / non-blocking subscriptions**: event-driven blocking waits via mailbox callbacks, or pure polling
- **Zero dynamic memory**: handles and data buffers are all statically provided by the user
- **Multiple lookup methods**: topics can be operated on by handle, by name, or by UUID (FNV-1a hash)
- **Pure C11 implementation**: depends only on `stdbool.h`, `stdint.h`, and `stdatomic.h`; easy to port

## Directory Layout

```
Inc/            Library header (simple_orb.h)
Src/            Library source (simple_orb.c)
_Example/Bare/  Bare-metal example (STM32H5 Nucleo, SequenceLock scheme)
_Example/RTOS/  FreeRTOS example (STM32H5 Nucleo, Mutex scheme)
```

## Quick Start

Add `Inc/simple_orb.h` and `Src/simple_orb.c` to your project, then follow the steps below.

### 1. Implement the critical-section protection functions

Create and subscribe operations need to protect internal linked lists; implement these according to your platform:

```c
void ORBCriticalEnter(void) { taskENTER_CRITICAL(); }  // bare metal: __disable_irq() works
void ORBCriticalExit(void)  { taskEXIT_CRITICAL(); }
```

### 2. Create a topic

Pick one of the two schemes; do not mix them:

```c
static uint32_t buffer;
static ORB_HANDLE_T orbHandle;

// SequenceLock scheme: wait is called while spinning in the being-written state to save
// power and may be NULL; with a single publisher, take/give may be NULL
ORBCreateUseSequenceLock("timestamp", &orbHandle, wait, NULL, NULL,
                         &buffer, sizeof(buffer));

// Mutex scheme: take/give must be provided
ORBCreateUseMutex("timestamp", &orbHandle, MutexTake, MutexGive,
                  &buffer, sizeof(buffer));
```

### 3. Publish data

```c
uint32_t data = 123;
ORBPublishByName("timestamp", &data, sizeof(data));
// ORBPublishByHandle / ORBPublishByUUID also work
```

### 4. Subscribe and receive data

```c
static ORB_SUBSCRIPTION_HANDLE_T subHandle;

// Non-blocking (polling) mode: pass NULL for send/receive
ORBSubscribeByName("timestamp", &subHandle, NULL, NULL);

bool updated;
ORBCheckNoBlock(&subHandle, &updated);
if (updated) {
    uint32_t value;
    ORBCopy(&subHandle, &value, sizeof(value));
}
```

Blocking (event-driven) mode: provide `send` (publisher-side notification) and `receive` (subscriber-side wait) callbacks at registration—for example, implemented with RTOS thread flags or semaphores—then use `ORBCheckBlock()` to wait for updates.

## Examples

- `_Example/Bare`: a button interrupt publishes a timestamp, while the main loop polls the subscription and prints it over UART
- `_Example/RTOS`: a button publishes a timestamp, and two subscriber tasks receive it in blocking mode (one drives an LED, the other prints over UART)

## License

[GNU AGPL v3](LICENSE)
