/**
 ******************************************************************************
 * @file simple_orb.c
 * @author lx
 * @version
 * @date 2026-07-12
 * @brief SimpleOBR - 一款使用发布/订阅模式的精简异步消息中间件
 ******************************************************************************
 * @attention
 *
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
 * @param pORBHandle  SimpleORB 句柄指针
 * @param delay       忙等延迟函数指针（在无锁模式下用于减少 CPU 占用）
 * @retval ORB_ERR_NONE    初始化成功
 * @retval ORB_ERR_INVALID_PARAM 参数无效
 */
ORB_ERR_T ORBInitUseSequenceLock(ORB_HANDLE_T * pORBHandle, void(*delay)(void))
{
    if (pORBHandle == NULL || delay == NULL) {
        return ORB_ERR_INVALID_PARAM;
    }

    pORBHandle->pORBSubscriptionList = NULL;

    pORBHandle->generation = 0;
    pORBHandle->data = NULL;
    pORBHandle->length = 0;

    pORBHandle->delay = delay;

    pORBHandle->mutex = NULL;
    pORBHandle->take = NULL;
    pORBHandle->give = NULL;

    return ORB_ERR_NONE;
}

/**
 * @brief 初始化 SimpleORB 句柄（Mutex 互斥同步模式）
 * 
 * @details 使用外部提供的 Mutex 对象实现线程安全。适用于不支持原子操作的平台。
 * @param pORBHandle  SimpleORB 句柄指针
 * @param mutex       Mutex 对象指针
 * @param take        获取锁函数指针（如 osMutexTake / sem_take）
 * @param give        释放锁函数指针（如 osMutexRelease / sem_give）
 * @retval ORB_ERR_NONE    初始化成功
 * @retval ORB_ERR_INVALID_PARAM 参数无效
 */
ORB_ERR_T ORBInitUseMutex(ORB_HANDLE_T * pORBHandle, void * mutex, void(*take)(void *), void(*give)(void *))
{
    if (pORBHandle == NULL || mutex == NULL || take == NULL || give == NULL) {
        return ORB_ERR_INVALID_PARAM;
    }

    pORBHandle->pORBSubscriptionList = NULL;

    pORBHandle->generation = 0;
    pORBHandle->data = NULL;
    pORBHandle->length = 0;

    pORBHandle->delay = NULL;

    pORBHandle->mutex = mutex;
    pORBHandle->take = take;
    pORBHandle->give = give;

    return ORB_ERR_NONE;
}

/**
 * @brief 发布数据到 Topic
 * 
 * @details 将 data 和 length 保存到句柄中，同时将 generation 递增。
 *          所有后续订阅者调用检查/拷贝时只会看到递增后的新代数，旧数据不会被读到。
 * @param pORBHandle  SimpleORB 句柄指针
 * @param data        发布的原始数据缓冲区指针
 * @param length      数据长度（字节），必须大于 0
 * @retval ORB_ERR_NONE    发布成功
 * @retval ORB_ERR_INVALID_PARAM 参数无效
 */
ORB_ERR_T ORBPublish(ORB_HANDLE_T * pORBHandle, void * data, int length)
{
    if (pORBHandle == NULL || data == NULL || length <= 0) {
        return ORB_ERR_INVALID_PARAM;
    }

    // 获取同步原语
    if (pORBHandle->mutex != NULL) {
        pORBHandle->take(pORBHandle->mutex);
    } else {
        // 写入前先将序列锁+1（变为奇数），标识"正在写入中"
        atomic_fetch_add_explicit(&pORBHandle->sequenceLock, 1, memory_order_acquire);
    }

    // 执行临界区：更新 shared state
    pORBHandle->data = data;
    pORBHandle->length = length;
    pORBHandle->generation = pORBHandle->generation + 1;

    // 释放同步原语
    if (pORBHandle->mutex != NULL) {
        pORBHandle->give(pORBHandle->mutex);
    } else {
        // 再+1 变为偶数，同时作为 release fence，保证上面的写操作对所有读者的可见性
        atomic_fetch_add_explicit(&pORBHandle->sequenceLock, 1, memory_order_release);
    }

    return ORB_ERR_NONE;
}

/**
 * @brief 注册订阅者到 Topic
 * 
 * @details 将订阅者挂入 Topic 的单链表尾部，并同步当前最新的 generation。
 * @param pORBHandle                    SimpleORB 句柄指针
 * @param pORBSubscriptionHandle        订阅者句柄指针
 * @param mail                          消息缓冲区（可为 NULL）
 * @param send                          发送回调（可整体为 NULL）
 * @param receive                       接收回调（可整体为 NULL）
 * @retval ORB_ERR_NONE    订阅成功
 * @retval ORB_ERR_INVALID_PARAM 参数无效
 */
ORB_ERR_T ORBSubscribe(ORB_HANDLE_T * pORBHandle, ORB_SUBSCRIPTION_HANDLE_T * pORBSubscriptionHandle, void * mail, void(*send)(void *), void(*receive)(void *))
{
    // mail/send/receive 要么全非空，要么全为空
    if (pORBHandle == NULL || pORBSubscriptionHandle == NULL || (!((mail == NULL && send == NULL && receive == NULL) || (mail != NULL && send != NULL && receive != NULL)))) {
        return ORB_ERR_INVALID_PARAM;
    }

    pORBSubscriptionHandle->pORBHandle = pORBHandle;
    pORBSubscriptionHandle->mail = mail;
    pORBSubscriptionHandle->send = send;
    pORBSubscriptionHandle->receive = receive;
    pORBSubscriptionHandle->next = NULL;
    pORBSubscriptionHandle->generation = pORBHandle->generation;

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

    return ORB_ERR_NONE;
}

/**
 * @brief 非阻塞方式检查数据是否更新
 * 
 * @details 等待并发布方完成本次写入后，读取当前 generation 与订阅者的缓存 generation 比较
 *          （仅在不使用 Mail 的非阻塞场景下生效）。不获取实际数据。
 * @param pORBSubscriptionHandle    订阅者句柄指针
 * @param updated                   输出参数：true=有新数据，false=无更新
 * @retval ORB_ERR_NONE    检查成功
 */
ORB_ERR_T ORBCheckNoBlock(ORB_SUBSCRIPTION_HANDLE_T * pORBSubscriptionHandle, bool * updated)
{
    ORB_GENERATION_T generation;
    uint32_t sequenceLock;

    // ---- 同步读取 current generation ----
    if (pORBSubscriptionHandle->pORBHandle->mutex != NULL) {
        // Mutex 模式：直接加锁→读数→解锁
        pORBSubscriptionHandle->pORBHandle->take(pORBSubscriptionHandle->pORBHandle->mutex);
        generation = pORBSubscriptionHandle->pORBHandle->generation;
        pORBSubscriptionHandle->pORBHandle->give(pORBSubscriptionHandle->pORBHandle->mutex);
    } else {
        // Sequence Lock 模式：等待写入期结束 → 读 generation → Spin Until Stable
        do {
            // 跳过奇数序列号阶段（写入中）
            while ((atomic_load_explicit(&pORBSubscriptionHandle->pORBHandle->sequenceLock, memory_order_relaxed) & 1u) != 0u) {
                pORBSubscriptionHandle->pORBHandle->delay();
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
 * @details 先调用 receive() 清空旧消息，然后进入阻塞等待直到有新数据到达。
 *          适用于带有 Mail 回调的阻塞订阅场景。
 * @note    若未配置 mail/send/receive 将返回 ORB_ERR_NO_MAIL
 * @param pORBSubscriptionHandle    订阅者句柄指针
 * @param updated                   输出参数：true=有新数据，false=无更新
 * @retval ORB_ERR_NONE            检查成功
 * @retval ORB_ERR_INVALID_PARAM   参数无效
 * @retval ORB_ERR_NO_MAIL         未配置 Mail 缓冲/回调
 */
ORB_ERR_T ORBCheckBlock(ORB_SUBSCRIPTION_HANDLE_T * pORBSubscriptionHandle, bool * updated)
{
    ORB_GENERATION_T generation;
    uint32_t sequenceLock;

    if (pORBSubscriptionHandle == NULL || updated == NULL) {
        return ORB_ERR_INVALID_PARAM;
    }

    // 有 Mail 则先调用 receive 回调（由用户清空上一轮消息内容）
    if (pORBSubscriptionHandle->mail == NULL) {
        return ORB_ERR_NO_MAIL;
    } else {
        pORBSubscriptionHandle->receive(pORBSubscriptionHandle->mail);
    }

    // ---- 同步读取 current generation（同 CheckNoBlock）----
    if (pORBSubscriptionHandle->pORBHandle->mutex != NULL) {
        pORBSubscriptionHandle->pORBHandle->take(pORBSubscriptionHandle->pORBHandle->mutex);
        generation = pORBSubscriptionHandle->pORBHandle->generation;
        pORBSubscriptionHandle->pORBHandle->give(pORBSubscriptionHandle->pORBHandle->mutex);
    } else {
        do {
            while ((atomic_load_explicit(&pORBSubscriptionHandle->pORBHandle->sequenceLock, memory_order_relaxed) & 1u) != 0u) {
                pORBSubscriptionHandle->pORBHandle->delay();
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
 * @details 保证复制到的是一份完整的数据帧（不会出现边读边写的情况）。
 *          复制完成后自动更新订阅者的 generation，防止下次重复处理同一帧。
 * @param pORBSubscriptionHandle    订阅者句柄指针
 * @param data                      目标缓冲区
 * @param length                    需要复制的数据长度（不能大于发布的 length）
 * @retval ORB_ERR_NONE            复制成功
 * @retval ORB_ERR_INVALID_PARAM   参数无效
 */
ORB_ERR_T ORBCopy(ORB_SUBSCRIPTION_HANDLE_T * pORBSubscriptionHandle, void * data, int length)
{
    uint32_t sequenceLock;

    if (pORBSubscriptionHandle == NULL || data == NULL || length <= 0 || length > pORBSubscriptionHandle->pORBHandle->length) {
        return ORB_ERR_INVALID_PARAM;
    }

    // ---- 复制数据（同 Check 逻辑，但额外执行 memcpy + 更新 generation）----
    if (pORBSubscriptionHandle->pORBHandle->mutex != NULL) {
        pORBSubscriptionHandle->pORBHandle->take(pORBSubscriptionHandle->pORBHandle->mutex);
        memcpy(data, pORBSubscriptionHandle->pORBHandle->data, length);
        pORBSubscriptionHandle->generation = pORBSubscriptionHandle->pORBHandle->generation;
        pORBSubscriptionHandle->pORBHandle->give(pORBSubscriptionHandle->pORBHandle->mutex);
    } else {
        do {
            while ((atomic_load_explicit(&pORBSubscriptionHandle->pORBHandle->sequenceLock, memory_order_relaxed) & 1u) != 0u) {
                pORBSubscriptionHandle->pORBHandle->delay();
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
