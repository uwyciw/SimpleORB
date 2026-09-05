/**
  ******************************************************************************
  * @file    simple_orb.h
  * @author  lx
  * @date    2026-07-12
  * @brief   -SimpleORB - 一款基于发布/订阅模式的轻量级异步消息中间件
  *          -支持 SequenceLock 和 Mutex 两种临界区保护方案，SequenceLock 方案适合裸机环境或写少读多场景使用，Mutex 方案适合在有 RTOS 且读写均衡场景使用
  ******************************************************************************
  *
  ******************************************************************************
***/

/* Includes ------------------------------------------------------------------*/
#include <string.h>
#include "simple_orb.h"

#ifndef __WEAK
  #if defined(__GNUC__) || defined(__ICCARM__)
    #define __WEAK __attribute__((weak))
  #elif defined(__CC_ARM) || defined(__ARMCC_VERSION)
    #define __WEAK __weak
  #else
    #define __WEAK
  #endif
#endif

static ORB_HANDLE_T * gORBHandleListHead = NULL;
static ORB_HANDLE_T * gORBHandleListTail = NULL;


/**
 * @brief 创建 SimpleORB 主题，并使用顺序锁作为临界区保护方案
 * @attention 不可与 ORBCreateUseMutex 同时使用
 * @param topic           主题名称，用于唯一标识主题
 * @param pORBHandle      SimpleORB 句柄指针
 * @param wait            每次查询到顺序锁处于“写入进行中”状态时，如该函数不为 NULL，则会调用该函数
 * @param take            仅对发布操作生效的互斥锁操作函数，如不需要可以为 NULL，订阅操作使用顺序锁进行互斥，与 give 要么同时为 NULL，要么同时非 NULL
 * @param give            仅对发布操作生效的互斥锁操作函数，如不需要可以为 NULL，订阅操作使用顺序锁进行互斥，与 give 要么同时为 NULL，要么同时非 NULL
 * @param buffer          用于存储主题数据的数据缓冲区指针，初始化后，该区域不应该被其他操作修改
 * @param length          数据缓冲区长度，必须 >0
 * @return ORB_ERR_T      ORB_ERR_NONE 表示初始化成功，ORB_ERR_INVALID_PARAM 表示参数无效
 */
ORB_ERR_T ORBCreateUseSequenceLock(const char * topic, ORB_HANDLE_T * pORBHandle, void(*wait)(void), void(*take)(void), void(*give)(void), void * buffer, int length)
{
    uint32_t uuid = ORBNameToUUID(topic);
    ORB_HANDLE_T * pORBHandleList = gORBHandleListHead;

    if (topic == NULL || pORBHandle == NULL || (take == NULL && give != NULL) || (take != NULL && give == NULL) || buffer == NULL || length <= 0) {
        return ORB_ERR_INVALID_PARAM;
    }

    pORBHandle->pORBSubscriptionList = NULL;

    pORBHandle->topic = topic;
    pORBHandle->uuid = uuid;
    pORBHandle->generation = 0;
    pORBHandle->data = buffer;
    pORBHandle->length = length;
    pORBHandle->next = NULL;

    atomic_init(&(pORBHandle->sequenceLock), 0);
    pORBHandle->wait = wait;
    pORBHandle->takeMutexSequenceLock = take;
    pORBHandle->giveMutexSequenceLock = give;

    pORBHandle->takeMutexORB = NULL;
    pORBHandle->giveMutexORB = NULL;

    ORBCriticalEnter();
    while (pORBHandleList != NULL) {
        if (pORBHandleList->uuid == uuid) {
            ORBCriticalExit();
            return ORB_ERR_TOPIC_EXIST;
        }    
        pORBHandleList = pORBHandleList->next;
    }    

    if (gORBHandleListHead == NULL) {
        gORBHandleListHead = pORBHandle;
        gORBHandleListTail = pORBHandle;
    } else {
        gORBHandleListTail->next = pORBHandle;
        gORBHandleListTail = pORBHandle;
    }
    ORBCriticalExit();

    return ORB_ERR_NONE;
}

/**
 * @brief 创建 SimpleORB 主题，并使用互斥锁作为临界区保护方案
 * @attention 不可与 ORBCreateUseSequenceLock 同时使用
 * @param topic           主题名称，用于唯一标识主题
 * @param pORBHandle      SimpleORB 句柄指针
 * @param take            获取 Mutex 的 P 操作回调，不可为 NULL
 * @param give            释放 Mutex 的 V 操作回调，不可为 NULL
 * @param buffer          用于存储主题数据的数据缓冲区指针，初始化后，该区域不应该被其他操作修改
 * @param length          数据缓冲区长度，必须 >0
 * @return ORB_ERR_T      ORB_ERR_NONE 表示初始化成功，ORB_ERR_INVALID_PARAM 表示参数无效
 */
ORB_ERR_T ORBCreateUseMutex(const char * topic, ORB_HANDLE_T * pORBHandle, void(*take)(void), void(*give)(void), void * buffer, int length)
{
    uint32_t uuid = ORBNameToUUID(topic);
    ORB_HANDLE_T * pORBHandleList = gORBHandleListHead;

    if (topic == NULL || pORBHandle == NULL || take == NULL || give == NULL || buffer == NULL || length <= 0) {
        return ORB_ERR_INVALID_PARAM;
    }

    pORBHandle->pORBSubscriptionList = NULL;

    pORBHandle->topic = topic;
    pORBHandle->uuid = uuid;
    pORBHandle->generation = 0;
    pORBHandle->data = buffer;
    pORBHandle->length = length;
    pORBHandle->next = NULL;

    atomic_init(&(pORBHandle->sequenceLock), 0);
    pORBHandle->wait = NULL;
    pORBHandle->takeMutexSequenceLock = NULL;
    pORBHandle->giveMutexSequenceLock = NULL;

    pORBHandle->takeMutexORB = take;
    pORBHandle->giveMutexORB = give;

    ORBCriticalEnter();
    while (pORBHandleList != NULL) {
        if (pORBHandleList->uuid == uuid) {
            ORBCriticalExit();
            return ORB_ERR_TOPIC_EXIST;
        }    
        pORBHandleList = pORBHandleList->next;
    }    

    if (gORBHandleListHead == NULL) {
        gORBHandleListHead = pORBHandle;
        gORBHandleListTail = pORBHandle;
    } else {
        gORBHandleListTail->next = pORBHandle;
        gORBHandleListTail = pORBHandle;
    }
    ORBCriticalExit();

    return ORB_ERR_NONE;
}

/**
 * @brief 发布数据到 Topic
 * @details 将 data 和 length 写入共享状态，递增 generation。
 *          完成后遍历所有订阅者，如果订阅者注册了 sendMail 回调函数，则会调用各自个订阅者注册的 sendMail() 函数。
 *          Sequence Lock 模式：+1 进入"写入中"态 → 写数据 → +1 恢复稳定态。
 *          Mutex 模式：获取临界区 → 写数据 → 释放临界区。
 * @param handle          SimpleORB 句柄指针
 * @param topic           主题名称
 * @param uuid            主题唯一标识
 * @param data            新数据的缓冲区指针（数据由使用者管理生命周期，发布者仅持有引用）
 * @param length          数据字节长度，必须 >0
 * @return ORB_ERR_T      ORB_ERR_NONE 表示发布成功，ORB_ERR_INVALID_PARAM 表示参数无效
 */
static ORB_ERR_T ORBPublish(ORB_HANDLE_T * handle, const char * topic, uint32_t uuid, void * data, int length)
{
    ORB_HANDLE_T * pORBHandle = NULL;
    ORB_HANDLE_T * pORBHandleList = gORBHandleListHead;

    if (data == NULL) {
        return ORB_ERR_INVALID_PARAM;
    }

    if (handle != NULL) {
        pORBHandle = handle;
    } else if (handle == NULL && topic != NULL) {
        ORBCriticalEnter();
        while (pORBHandleList != NULL) {
            if (strcmp(pORBHandleList->topic, topic) == 0) {
                pORBHandle = pORBHandleList;
                break;
            }
            pORBHandleList = pORBHandleList->next;
        }
        ORBCriticalExit();
        if (pORBHandle == NULL) {
            return ORB_ERR_TOPIC_NOT_EXIST;
        }
    } else {
        ORBCriticalEnter();
        while (pORBHandleList != NULL) {
            if (pORBHandleList->uuid == uuid) {
                pORBHandle = pORBHandleList;
                break;
            }
            pORBHandleList = pORBHandleList->next;
        }
        ORBCriticalExit();
        if (pORBHandle == NULL) {
            return ORB_ERR_TOPIC_NOT_EXIST;
        }
    }

    if (length <= 0 || pORBHandle->length != length) {
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
        atomic_fetch_add_explicit(&pORBHandle->sequenceLock, 1, memory_order_relaxed);
    }

    /* --- 临界区核心操作：更新 Topic 的共享数据帧 --- */
    memcpy(pORBHandle->data, data, length);
    pORBHandle->generation = pORBHandle->generation + 1;

    /* --- 释放同步原语 --- */
    if (pORBHandle->giveMutexORB != NULL) {
        /* Mutex 模式：释放互斥锁 */
        pORBHandle->giveMutexORB();
    } else {
        /* Sequence Lock 模式：序列锁再+1（偶数值），既表示"写入完成 */
        atomic_fetch_add_explicit(&pORBHandle->sequenceLock, 1, memory_order_relaxed);
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
ORB_ERR_T ORBPublishByHandle(ORB_HANDLE_T * pORBHandle, void * data, int length)
{
    if (pORBHandle == NULL) {
        return ORB_ERR_INVALID_PARAM;
    }
    return ORBPublish(pORBHandle, NULL, 0, data, length);
}
ORB_ERR_T ORBPublishByName(const char * topic, void * data, int length)
{
    if (topic == NULL) {
        return ORB_ERR_INVALID_PARAM;
    }
    return ORBPublish(NULL, topic, 0, data, length);
}
ORB_ERR_T ORBPublishByUUID(uint32_t uuid, void * data, int length)
{
    return ORBPublish(NULL, NULL, uuid, data, length);
}

/**
 * @brief 注册订阅者到 Topic
 * @details 将订阅者节点尾插到 Topic 的 subscriber 链表中，并拷贝当前最新 generation。
 *          阻塞模式（send/receive 非空）：适用于事件驱动场景，订阅者阻塞等待 + 发布者发送回调发送事件。
 *          非阻塞模式（send/receive 均为 NULL）：适用于纯轮询场景。
 * @param handle                      SimpleORB 句柄指针
 * @param topic                       主题名称
 * @param uuid                        主题唯一标识
 * @param pORBSubscriptionHandle      订阅者句柄指针
 * @param send                        发送事件回调函数指针，若不需要可以为 NULL
 * @param receive                     阻塞等待事件的回调函数指针，若不需要可以为 NULL
 * @return ORB_ERR_T                  ORB_ERR_NONE 表示订阅成功，ORB_ERR_INVALID_PARAM 表示参数无效
 */
static ORB_ERR_T ORBSubscribe(ORB_HANDLE_T * handle, const char * topic, uint32_t uuid, ORB_SUBSCRIPTION_HANDLE_T * pORBSubscriptionHandle, void(*send)(void), void(*receive)(void))
{
    ORB_HANDLE_T * pORBHandle = NULL;
    ORB_HANDLE_T * pORBHandleList = gORBHandleListHead;

    if (pORBSubscriptionHandle == NULL) {
        return ORB_ERR_INVALID_PARAM;
    }

    if (handle != NULL) {
        pORBHandle = handle;
    } else if (handle == NULL && topic != NULL) {
        uuid = ORBNameToUUID(topic);
        ORBCriticalEnter();
        while (pORBHandleList != NULL) {
            if (pORBHandleList->uuid == uuid) {
                pORBHandle = pORBHandleList;
                break;
            }
            pORBHandleList = pORBHandleList->next;
        }
        ORBCriticalExit();
        if (pORBHandle == NULL) {
            return ORB_ERR_TOPIC_NOT_EXIST;
        }
    } else {
        ORBCriticalEnter();
        while (pORBHandleList != NULL) {
            if (pORBHandleList->uuid == uuid) {
                pORBHandle = pORBHandleList;
                break;
            }
            pORBHandleList = pORBHandleList->next;
        }
        ORBCriticalExit();
        if (pORBHandle == NULL) {
            return ORB_ERR_TOPIC_NOT_EXIST;
        }
    }
    
    pORBSubscriptionHandle->pORBHandle = pORBHandle;
    pORBSubscriptionHandle->sendMail = send;
    pORBSubscriptionHandle->receiveMail = receive;
    pORBSubscriptionHandle->next = NULL;
    pORBSubscriptionHandle->generation = pORBHandle->generation;
    
    ORBCriticalEnter();
    if (pORBHandle->pORBSubscriptionList == NULL) {
        pORBHandle->pORBSubscriptionList = pORBSubscriptionHandle;
    } else {
        ORB_SUBSCRIPTION_HANDLE_T * pCurrent = pORBHandle->pORBSubscriptionList;
        while (pCurrent->next != NULL) {
            pCurrent = pCurrent->next;
        }
        pCurrent->next = pORBSubscriptionHandle;
    }
    ORBCriticalExit();

    return ORB_ERR_NONE;
}
ORB_ERR_T ORBSubscribeByHandle(ORB_HANDLE_T * pORBHandle, ORB_SUBSCRIPTION_HANDLE_T * pORBSubscriptionHandle, void(*send)(void), void(*receive)(void))
{
    if (pORBHandle == NULL) {
        return ORB_ERR_INVALID_PARAM;
    }
    return ORBSubscribe(pORBHandle, NULL, 0, pORBSubscriptionHandle, send, receive);
}
ORB_ERR_T ORBSubscribeByName(const char * topic, ORB_SUBSCRIPTION_HANDLE_T * pORBSubscriptionHandle, void(*send)(void), void(*receive)(void))
{
    if (topic == NULL) {
        return ORB_ERR_INVALID_PARAM;
    }
    return ORBSubscribe(NULL, topic, 0, pORBSubscriptionHandle, send, receive);
}
ORB_ERR_T ORBSubscribeByUUID(uint32_t uuid, ORB_SUBSCRIPTION_HANDLE_T * pORBSubscriptionHandle, void(*send)(void), void(*receive)(void))
{
    return ORBSubscribe(NULL, NULL, uuid, pORBSubscriptionHandle, send, receive);
}

/**
 * @brief 非阻塞方式检查数据是否更新
 * @details 读取当前 Topic 的 generation 并与订阅者缓存值比较。
 *          Sequence Lock 模式下：等到稳定态 → 读 generation → Spin Until Stable 验证一致性。
 *          Mutex 模式下：加锁 → 读 generation → 解锁。
 *          仅做增量检测，不拷贝数据。
 * @param pORBSubscriptionHandle    订阅者句柄指针
 * @param updated                   输出参数：true 表示有新数据待处理，false 表示与上次一致无变化
 * @return ORB_ERR_T                ORB_ERR_NONE 表示检查成功
 */
ORB_ERR_T ORBCheckNoBlock(ORB_SUBSCRIPTION_HANDLE_T * pORBSubscriptionHandle, bool * updated)
{
    unsigned int generation;
    uint32_t sequenceLock;

    if (pORBSubscriptionHandle == NULL || updated == NULL) {
        return ORB_ERR_INVALID_PARAM;
    }

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
 * @details 调用 receiveMail() 阻塞等待发布者在更新数据时通过 sendMail() 发布邮箱事件。
 * @param pORBSubscriptionHandle    订阅者句柄指针
 * @param updated                   输出参数：true 表示有新数据，false 表示无更新
 * @return ORB_ERR_T                ORB_ERR_NONE 表示检查成功
 * @return ORB_ERR_NO_MAIL          注册订阅时，未配置 receiveMail 回调函数
 */
ORB_ERR_T ORBCheckBlock(ORB_SUBSCRIPTION_HANDLE_T * pORBSubscriptionHandle, bool * updated)
{
    unsigned int generation;
    uint32_t sequenceLock;

    if (pORBSubscriptionHandle == NULL || updated == NULL) {
        return ORB_ERR_INVALID_PARAM;
    }

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
 * @details 以原子/互斥方式安全地拷贝一帧完整数据：
 *          - Sequence Lock 模式：Spin-Until-Stable 确认写入完成 → memcpy → 更新本地 generation
 *          - Mutex 模式：获取临界区 → memcpy → 更新本地 generation → 释放临界区
 *          复制完毕后自动更新订阅者的 generation 字段。
 * @param pORBSubscriptionHandle    订阅者句柄指针
 * @param data                      用于存储主题数据的缓存区指针
 * @param length                    缓存区长度，必须 >= Topic 数据的长度
 * @return ORB_ERR_T                ORB_ERR_NONE 表示复制成功，ORB_ERR_INVALID_PARAM 表示参数无效
 */
ORB_ERR_T ORBCopy(ORB_SUBSCRIPTION_HANDLE_T * pORBSubscriptionHandle, void * data, int length)
{
    uint32_t sequenceLock;

    if (pORBSubscriptionHandle == NULL || data == NULL || length <= 0 || length < pORBSubscriptionHandle->pORBHandle->length) {
        return ORB_ERR_INVALID_PARAM;
    }

    // ---- 复制数据（同 Check 逻辑，但额外执行 memcpy + 更新 generation）----
    if (pORBSubscriptionHandle->pORBHandle->takeMutexORB != NULL) {
        pORBSubscriptionHandle->pORBHandle->takeMutexORB();
        memcpy(data, pORBSubscriptionHandle->pORBHandle->data, pORBSubscriptionHandle->pORBHandle->length);
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
            memcpy(data, pORBSubscriptionHandle->pORBHandle->data, pORBSubscriptionHandle->pORBHandle->length);
            pORBSubscriptionHandle->generation = pORBSubscriptionHandle->pORBHandle->generation;
            if (sequenceLock == atomic_load_explicit(&pORBSubscriptionHandle->pORBHandle->sequenceLock, memory_order_relaxed)) {
                break;
            }
        } while (1);
    }

    return ORB_ERR_NONE;
}

/**
 * @brief 通过FNV-1a将主题名称转换为32bit唯一标识
 * @param name 主题名称
 * @return 32bit唯一标识
 */
uint32_t ORBNameToUUID(const char *name)
{
#define FNV1A_OFFSET_BASIS  UINT32_C(0x811C9DC5)
#define FNV1A_PRIME         UINT32_C(0x01000193)
    uint32_t hash = FNV1A_OFFSET_BASIS;
    
    if (name == NULL) {
        return 0;
    }

    for (const unsigned char *p = (const unsigned char *)name; *p != '\0'; ++p) {
        hash ^= (uint32_t)*p;
        hash *= FNV1A_PRIME;
    }
    
    return hash ? hash : 1;
}

/**
 * @brief 临界区保护函数，由用户提供具体实现，在执行创建和订阅操作时，保护主题链表
 */
__WEAK void ORBCriticalEnter(void) {}
__WEAK void ORBCriticalExit(void) {}
