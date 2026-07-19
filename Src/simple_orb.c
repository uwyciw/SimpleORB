/**
  ******************************************************************************
  * @file    simple_orb.c
  * @author  lx
  * @version V1.0.0
  * @date    2026-07-12
  * @brief   SimpleORB - 一款基于发布/订阅模式的轻量级异步消息中间件实现
  *          支持两种同步模式：
  *            - Sequence Lock（无锁原子序列锁，基于 C11 <stdatomic.h>）
  *            - Mutex（互斥锁，通过用户提供的 take/give 回调函数操作）
  *          每个 Topic 维护一个生成号（generation），发布者递增该号码，
  *          订阅者通过比较生成号判断数据是否更新，从而实现线程安全的数据交换。
  ******************************************************************************
  * @attention
  * 本代码为自由软件，可在 GNU Affero General Public License v3 条款下分发和使用。
  ******************************************************************************
  */

  /* Includes ------------------------------------------------------------------*/
#include <string.h>
#include "simple_orb.h"

/********************************************************************************
*                          Public Functions
********************************************************************************/

/**
 * @brief 初始化 SimpleORB 句柄（Sequence Lock 无锁同步模式）
 *
 * @details 使用 C11 atomic sequence lock 实现线程安全，无需互斥锁。
 *          写入方交替设置奇数/偶数序列号：
 *            - 偶数 = 读者可以安全读取的阶段
 *            - 奇数 = 写入方正在修改数据的阶段
 *          读取方通过判断序列锁的最低位是否为 0 来避免读到脏数据。
 *          take/give 回调可选为 NULL，此时仅依赖原子操作完成同步。
 * @param pORBHandle      SimpleORB 句柄指针
 * @param wait            忙等延迟函数指针（在无锁模式下用于减少 CPU 占用），可为 NULL
 * @param take            获取临界区资源回调，与 give 要么同时为 NULL，要么同时非 NULL
 * @param give            释放临界区资源回调，与 take 要么同时为 NULL，要么同时非 NULL
 * @retval ORB_ERR_NONE              初始化成功
 * @retval ORB_ERR_INVALID_PARAM     pORBHandle 为空或 take/give 仅有一个非空时返回此值
 */
ORB_ERR_T ORBInitUseSequenceLock(ORB_HANDLE_T * pORBHandle, void(*wait)(void), void(*take)(void), void(*give)(void))
{
    if (pORBHandle == NULL || (take == NULL && give != NULL) || (take != NULL && give == NULL)) {
        return ORB_ERR_INVALID_PARAM;
    }

    pORBHandle->pORBSubscriptionList = NULL;

    pORBHandle->generation = 0;
    pORBHandle->data = NULL;
    pORBHandle->length = 0;

    pORBHandle->wait = wait;
    pORBHandle->takeMutexSequenceLock = take;
    pORBHandle->giveMutexSequenceLock = give;

    pORBHandle->takeMutexORB = NULL;
    pORBHandle->giveMutexORB = NULL;

    return ORB_ERR_NONE;
}

/**
 * @brief 初始化 SimpleORB 句柄（Mutex 互斥同步模式）
 *
 * @details 使用外部提供的 Mutex 回调实现线程安全。适用于不支持原子操作的平台。
 *          take/give 回调必须在每次调用临界区操作前/后执行，确保数据一致性。
 * @param pORBHandle  SimpleORB 句柄指针
 * @param take        获取锁函数指针（如 osMutexTake / sem_wait），不可为 NULL
 * @param give        释放锁函数指针（如 osMutexRelease / sem_post），不可为 NULL
 * @retval ORB_ERR_NONE          初始化成功
 * @retval ORB_ERR_INVALID_PARAM Take 或 Give 为 NULL 时返回此值
 */
ORB_ERR_T ORBInitUseMutex(ORB_HANDLE_T * pORBHandle, void(*take)(void), void(*give)(void))
{
    if (pORBHandle == NULL || take == NULL || give == NULL) {
        return ORB_ERR_INVALID_PARAM;
    }

    pORBHandle->pORBSubscriptionList = NULL;

    pORBHandle->generation = 0;
    pORBHandle->data = NULL;
    pORBHandle->length = 0;

    pORBHandle->wait = NULL;
    pORBHandle->takeMutexSequenceLock = NULL;
    pORBHandle->giveMutexSequenceLock = NULL;

    pORBHandle->takeMutexORB = take;
    pORBHandle->giveMutexORB = give;

    return ORB_ERR_NONE;
}

/**
 * @brief 发布数据到 Topic
 *
 * @details 将 data 和 length 保存到句柄中，同时将 generation 递增。
 *          所有后续订阅者调用检查/拷贝时只会看到递增后的新代数，旧数据不会被读到。
 *          Sequence Lock 模式：+1 进入写入态 → 写数据 → +1 恢复稳定态，并通知所有订阅者。
 *          Mutex 模式：获取临界区 → 写数据 → 释放临界区，并通知所有订阅者。
 * @param pORBHandle      SimpleORB 句柄指针
 * @param data            发布的原始数据缓冲区指针（由发布者管理生命周期）
 * @param length          数据长度（字节），必须大于 0
 * @retval ORB_ERR_NONE          发布成功
 * @retval ORB_ERR_INVALID_PARAM pORBHandle、data 为空或 length <= 0 时返回此值
 */
ORB_ERR_T ORBPublish(ORB_HANDLE_T * pORBHandle, void * data, int length)
{
    if (pORBHandle == NULL || data == NULL || length <= 0) {
        return ORB_ERR_INVALID_PARAM;
    }

    /* --- 进入临界区：根据模式选择同步原语 --- */
    if (pORBHandle->takeMutexORB != NULL) {
        /* Mutex 模式：调用外部提供的互斥锁获取回调 */
        pORBHandle->takeMutexORB();
    } else {
        /* Sequence Lock 模式：可选地先抢占序列锁平台的共享资源 */
        if (pORBHandle->takeMutexSequenceLock != NULL) {
            pORBHandle->takeMutexSequenceLock();
        }
        /* 将 sequenceLock +1（奇数值），表示"写入进行中"，阻塞后续读取者 */
        atomic_fetch_add_explicit(&pORBHandle->sequenceLock, 1, memory_order_acquire);
    }

    /* --- 临界区核心操作：更新 Topic 的共享数据帧 --- */
    pORBHandle->data = data;
    pORBHandle->length = length;
    pORBHandle->generation = pORBHandle->generation + 1;

    /* --- 释放同步原语 --- */
    if (pORBHandle->giveMutexORB != NULL) {
        /* Mutex 模式：释放互斥锁 */
        pORBHandle->giveMutexORB();
    } else {
        /* Sequence Lock 模式：序列锁再+1（偶数值），既表示"写入完成"也作为 release fence，
         * 确保上述 data/length/generation 的修改对所有读者可见 */
        atomic_fetch_add_explicit(&pORBHandle->sequenceLock, 1, memory_order_release);
        if (pORBHandle->giveMutexSequenceLock != NULL) {
            pORBHandle->giveMutexSequenceLock();
        }
    }

    /* --- 通知所有订阅者：遍历链表并触发各自 sendMail() 回调 --- */
    for (ORB_SUBSCRIPTION_HANDLE_T * pCurrent = pORBHandle->pORBSubscriptionList; pCurrent != NULL; pCurrent = pCurrent->next) {
        if (pCurrent->sendMail != NULL) {
            pCurrent->sendMail();
        }
    }

    return ORB_ERR_NONE;
}

/**
 * @brief 注册订阅者到 Topic
 *
 * @details 将订阅者挂入 Topic 的单链表尾部，并同步当前最新的 generation。
 *          send/receive 回调要么同时为 NULL（非阻塞模式），要么同时非 NULL（阻塞模式）。
 * @param pORBHandle                  SimpleORB 句柄指针
 * @param pORBSubscriptionHandle      订阅者句柄指针
 * @param send                        发送回调函数指针（阻塞模式下使用），为空时 receive 也必须为空
 * @param receive                     接收回调函数指针（阻塞模式下使用），为空时 send 也必须为空
 * @retval ORB_ERR_NONE              订阅成功
 * @retval ORB_ERR_INVALID_PARAM     参数为空或 send/receive 仅有一个非空时返回此值
 */
ORB_ERR_T ORBSubscribe(ORB_HANDLE_T * pORBHandle, ORB_SUBSCRIPTION_HANDLE_T * pORBSubscriptionHandle, void(*send)(void), void(*receive)(void))
{
    // send/receive 要么全非空，要么全为空
    if (pORBHandle == NULL || pORBSubscriptionHandle == NULL || (send != NULL && receive == NULL) || (send == NULL && receive != NULL)) {
        return ORB_ERR_INVALID_PARAM;
    }

    pORBSubscriptionHandle->pORBHandle = pORBHandle;
    pORBSubscriptionHandle->sendMail = send;
    pORBSubscriptionHandle->receiveMail = receive;
    pORBSubscriptionHandle->next = NULL;
    pORBSubscriptionHandle->generation = pORBHandle->generation;

    if (pORBHandle->takeMutexORB != NULL) {
        pORBHandle->takeMutexORB();
    }

    // 尾插法加入订阅者链表
    if (pORBHandle->pORBSubscriptionList == NULL) {
        pORBHandle->pORBSubscriptionList = pORBSubscriptionHandle;
    } else {
        ORB_SUBSCRIPTION_HANDLE_T * pCurrent = pORBHandle->pORBSubscriptionList;
        while (pCurrent->next != NULL) {
            pCurrent = pCurrent->next;
        }
        pCurrent->next = pORBSubscriptionHandle;
    }

    if (pORBHandle->giveMutexORB != NULL) {
        pORBHandle->giveMutexORB();
    }

    return ORB_ERR_NONE;
}

/**
 * @brief 非阻塞方式检查数据是否更新
 *
 * @details 等待并发布方完成本次写入后，读取当前 generation 与订阅者的缓存 generation 比较。
 *          Sequence Lock 模式：Spin-Until-Stable 确保读到一致的数据 → 比较代数。
 *          Mutex 模式：加锁 → 读 generation → 解锁 → 比较代数。
 *          仅不做数据拷贝，适用于轮询场景。
 * @param pORBSubscriptionHandle    订阅者句柄指针
 * @param updated                   输出参数：true 表示有新数据待处理，false 表示与上次一致无变化
 * @retval ORB_ERR_NONE            检查成功
 */
ORB_ERR_T ORBCheckNoBlock(ORB_SUBSCRIPTION_HANDLE_T * pORBSubscriptionHandle, bool * updated)
{
    unsigned int generation;
    uint32_t sequenceLock;

    // ---- 同步读取 current generation ----
    if (pORBSubscriptionHandle->pORBHandle->takeMutexORB != NULL) {
        // Mutex 模式：直接加锁→读数→解锁
        pORBSubscriptionHandle->pORBHandle->takeMutexORB();
        generation = pORBSubscriptionHandle->pORBHandle->generation;
        pORBSubscriptionHandle->pORBHandle->giveMutexORB();
    } else {
        // Sequence Lock 模式：等待写入期结束 → 读 generation → Spin Until Stable
        do {
            // 跳过奇数序列号阶段（写入中）
            while ((atomic_load_explicit(&pORBSubscriptionHandle->pORBHandle->sequenceLock, memory_order_relaxed) & 1u) != 0u) {
                // 等待写入完成
                if (pORBSubscriptionHandle->pORBHandle->wait != NULL) {
                    pORBSubscriptionHandle->pORBHandle->wait();
                }
            }
            sequenceLock = atomic_load_explicit(&pORBSubscriptionHandle->pORBHandle->sequenceLock, memory_order_relaxed);
            generation = pORBSubscriptionHandle->pORBHandle->generation;
            // 二次验证：如果 sequenceLock 在读取过程中未发生变化，说明读到的值是一致的
            if (sequenceLock == atomic_load_explicit(&pORBSubscriptionHandle->pORBHandle->sequenceLock, memory_order_relaxed)) {
                break;
            }
        } while (1);
    }

    // 比较代数检测更新
    if (pORBSubscriptionHandle->generation != generation) {
        *updated = true;
    } else {
        *updated = false;
    }

    return ORB_ERR_NONE;
}

/**
 * @brief 阻塞方式检查数据是否更新
 *
 * @details 先调用 receiveMail() 回调清空旧消息状态，然后同步读取并比较 generation。
 *          Sequence Lock 模式：Spin-Until-Stable 确保读到一致的数据 → 比较代数。
 *          Mutex 模式：加锁 → 读 generation → 解锁 → 比较代数。
 *          适用于带有 Mail/事件机制的阻塞订阅场景。
 * @param pORBSubscriptionHandle    订阅者句柄指针
 * @param updated                   输出参数：true 表示有新数据待处理，false 表示无更新
 * @retval ORB_ERR_NONE            检查成功
 * @retval ORB_ERR_INVALID_PARAM   pORBSubscriptionHandle 或 updated 为 NULL 时返回此值
 * @retval ORB_ERR_NO_MAIL         未配置 receiveMail 回调（即非阻塞模式）时返回此值
 */
ORB_ERR_T ORBCheckBlock(ORB_SUBSCRIPTION_HANDLE_T * pORBSubscriptionHandle, bool * updated)
{
    unsigned int generation;
    uint32_t sequenceLock;

    if (pORBSubscriptionHandle == NULL || updated == NULL) {
        return ORB_ERR_INVALID_PARAM;
    }

    // 有 receiveMail 则先调用其回调（由用户清空上一轮消息内容）
    if (pORBSubscriptionHandle->receiveMail == NULL) {
        return ORB_ERR_NO_MAIL;
    } else {
        pORBSubscriptionHandle->receiveMail();
    }

    // ---- 同步读取 current generation（同 CheckNoBlock）----
    if (pORBSubscriptionHandle->pORBHandle->takeMutexORB != NULL) {
        pORBSubscriptionHandle->pORBHandle->takeMutexORB();
        generation = pORBSubscriptionHandle->pORBHandle->generation;
        pORBSubscriptionHandle->pORBHandle->giveMutexORB();
    } else {
        do {
            while ((atomic_load_explicit(&pORBSubscriptionHandle->pORBHandle->sequenceLock, memory_order_relaxed) & 1u) != 0u) {
                // 等待写入完成
                if (pORBSubscriptionHandle->pORBHandle->wait != NULL) {
                    pORBSubscriptionHandle->pORBHandle->wait();
                }
            }
            sequenceLock = atomic_load_explicit(&pORBSubscriptionHandle->pORBHandle->sequenceLock, memory_order_relaxed);
            generation = pORBSubscriptionHandle->pORBHandle->generation;
            if (sequenceLock == atomic_load_explicit(&pORBSubscriptionHandle->pORBHandle->sequenceLock, memory_order_relaxed)) {
                break;
            }
        } while (1);
    }

    // 比较代数检测更新
    if (pORBSubscriptionHandle->generation != generation) {
        *updated = true;
    } else {
        *updated = false;
    }

    return ORB_ERR_NONE;
}

/**
 * @brief 将 Topic 上的最新数据复制到指定缓冲区
 *
 * @details 以原子/互斥方式安全地拷贝一帧完整数据（不会出现边读边写）。
 *          Sequence Lock 模式：Spin-Until-Stable → memcpy → 更新本地 generation。
 *          Mutex 模式：获取临界区 → memcpy → 更新本地 generation → 释放临界区。
 *          复制完成后自动更新订阅者的 generation，防止下次重复处理同一帧。
 * @param pORBSubscriptionHandle    订阅者句柄指针
 * @param data                      目标缓冲区指针，需由调用者分配并确保容量 >= length
 * @param length                    需要复制的数据字节长度，必须 <= Topic 当前数据的真实长度
 * @retval ORB_ERR_NONE            复制成功
 * @retval ORB_ERR_INVALID_PARAM   参数为空、length <= 0 或 length > topic length 时返回此值
 */
ORB_ERR_T ORBCopy(ORB_SUBSCRIPTION_HANDLE_T * pORBSubscriptionHandle, void * data, int length)
{
    uint32_t sequenceLock;

    if (pORBSubscriptionHandle == NULL || data == NULL || length <= 0 || length > pORBSubscriptionHandle->pORBHandle->length) {
        return ORB_ERR_INVALID_PARAM;
    }

    // ---- 复制数据（同 Check 逻辑，但额外执行 memcpy + 更新 generation）----
    if (pORBSubscriptionHandle->pORBHandle->takeMutexORB != NULL) {
        pORBSubscriptionHandle->pORBHandle->takeMutexORB();
        memcpy(data, pORBSubscriptionHandle->pORBHandle->data, length);
        pORBSubscriptionHandle->generation = pORBSubscriptionHandle->pORBHandle->generation;
        pORBSubscriptionHandle->pORBHandle->giveMutexORB();
    } else {
        do {
            while ((atomic_load_explicit(&pORBSubscriptionHandle->pORBHandle->sequenceLock, memory_order_relaxed) & 1u) != 0u) {
                // 等待写入完成
                if (pORBSubscriptionHandle->pORBHandle->wait != NULL) {
                    pORBSubscriptionHandle->pORBHandle->wait();
                }
            }
            sequenceLock = atomic_load_explicit(&pORBSubscriptionHandle->pORBHandle->sequenceLock, memory_order_relaxed);
            memcpy(data, pORBSubscriptionHandle->pORBHandle->data, length);
            pORBSubscriptionHandle->generation = pORBSubscriptionHandle->pORBHandle->generation;
            if (sequenceLock == atomic_load_explicit(&pORBSubscriptionHandle->pORBHandle->sequenceLock, memory_order_relaxed)) {
                break;
            }
        } while (1);
    }

    return ORB_ERR_NONE;
}
