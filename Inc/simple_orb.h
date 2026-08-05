/**
  ******************************************************************************
  * @file    simple_orb.h
  * @author  lx
  * @version V1.0.0
  * @date    2026-07-12
  * @brief   -SimpleORB - 一款基于发布/订阅模式的轻量级异步消息中间件
  *          -支持 SequenceLock 和 Mutex 两种临界区保护方案，SequenceLock 方案适合裸机环境或写少读多场景使用，Mutex 方案适合在有 RTOS 且读写均衡场景使用
  ******************************************************************************
  *
  ******************************************************************************
  */

#ifndef _SIMPLE_ORB_H_
#define _SIMPLE_ORB_H_

  /* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

/**
 * @brief SimpleORB 返回码定义
 */
typedef enum {
    ORB_ERR_NONE = 0,       /**< 操作成功 */
    ORB_ERR_INVALID_PARAM,  /**< 参数无效 */
    ORB_ERR_NO_MAIL,        /**< 使用阻塞等待但未在初始化时配置邮箱 */
} ORB_ERR_T;

/**
 * @brief SimpleORB 句柄前向声明
 */
struct orb_handle_t;

/**
 * @brief 订阅句柄前向声明
 */
struct orb_subscription_handle_t;

/**
 * @brief SimpleORB 句柄结构体（代表一个 Topic）
 *        每个 Topic 独立维护一份共享数据，支持多个订阅者异步读取。
 */
typedef struct orb_handle_t {
    struct orb_subscription_handle_t * pORBSubscriptionList;    /**< 订阅者链表的头节点指针 */

    unsigned int generation;                                    /**< ORB 更新代数，循环计数 */
    void * data;                                                /**< 最近一次发布的数据缓冲区指针，由发布者提供 */
    int length;                                                 /**< 最近一次发布的数据字节长度 */

    /* --- Sequence Lock --- */
    atomic_uint sequenceLock;                                   /**< 原子序列锁：偶数值表示"可读稳定态"，奇数值表示"写入进行中" */
    void(*wait)(void);                                          /**< 忙等延迟函数指针，仅在 Sequence Lock 模式下有效，用于空转时降低 CPU 占用，如初始化为NULL则不使用 */
    void(*takeMutexSequenceLock)(void);                         /**< 序列锁模式的 P 操作（获取临界区），仅在publish时使用，如不需要可以设置为NULL */
    void(*giveMutexSequenceLock)(void);                         /**< 序列锁模式的 V 操作（释放临界区），仅在publish时使用, 如不需要可以设置为NULL */

    /* --- Mutex --- */
    void(*takeMutexORB)(void);                                  /**< Mutex 模式的 P 操作（获取临界区）, publish 和 copy 时使用 */
    void(*giveMutexORB)(void);                                  /**< Mutex 模式的 V 操作（释放临界区）, publish 和 copy 时使用 */
} ORB_HANDLE_T;

/**
 * @brief 订阅句柄结构体
 */
typedef struct orb_subscription_handle_t {
    struct orb_handle_t * pORBHandle;                           /**< 反向引用：指向关联 Topic 的 ORB_HANDLE_T 句柄 */
    struct orb_subscription_handle_t * next;                    /**< 单向链表后继节点指针，由内部链表管理，用户无需修改 */

    unsigned int generation;                                    /**< 订阅者最近一次 copy 的数据的代数，在每次执行 ORBCopy 时更新该字段 */

    void(*sendMail)(void);                                      /**< 订阅者如需要发布者在发布 ORB 时，通过邮箱通知自己，则应在注册订阅时提供邮箱的发送和接收函数，如不需要可以设置为 NULL */
    void(*receiveMail)(void);                                   /**< 订阅者如需要发布者在发布 ORB 时，通过邮箱通知自己，则应在注册订阅时提供邮箱的发送和接收函数，如不需要可以设置为 NULL */
} ORB_SUBSCRIPTION_HANDLE_T;

 /**
  * @brief 初始化 SimpleORB 句柄，并使用顺序锁作为临界区保护方案
  * @attention 不可与 ORBInitUseMutex 同时使用
  * @param pORBHandle      SimpleORB 句柄指针
  * @param wait            每次查询到顺序锁处于“写入进行中”状态时，如该函数不为 NULL，则会调用该函数
  * @param take            仅对发布操作生效的互斥锁操作函数，如不需要可以为 NULL，订阅操作使用顺序锁进行互斥，与 give 要么同时为 NULL，要么同时非 NULL
  * @param give            仅对发布操作生效的互斥锁操作函数，如不需要可以为 NULL，订阅操作使用顺序锁进行互斥，与 give 要么同时为 NULL，要么同时非 NULL
  * @return ORB_ERR_T      ORB_ERR_NONE 表示初始化成功，ORB_ERR_INVALID_PARAM 表示参数无效
  */
ORB_ERR_T ORBInitUseSequenceLock(ORB_HANDLE_T * pORBHandle, void(*wait)(void), void(*take)(void), void(*give)(void));

/**
 * @brief 初始化 SimpleORB 句柄，并使用互斥锁作为临界区保护方案
 * @attention 不可与 ORBInitUseSequenceLock 同时使用
 * @param pORBHandle      SimpleORB 句柄指针
 * @param take            获取 Mutex 的 P 操作回调，不可为 NULL
 * @param give            释放 Mutex 的 V 操作回调，不可为 NULL
 * @return ORB_ERR_T      ORB_ERR_NONE 表示初始化成功，ORB_ERR_INVALID_PARAM 表示参数无效
 */
ORB_ERR_T ORBInitUseMutex(ORB_HANDLE_T * pORBHandle, void(*take)(void), void(*give)(void));

/**
 * @brief 发布数据到 Topic
 * @details 将 data 和 length 写入共享状态，递增 generation。
 *          完成后遍历所有订阅者，如果订阅者注册了 sendMail 回调函数，则会调用各自个订阅者注册的 sendMail() 函数。
 *          Sequence Lock 模式：+1 进入"写入中"态 → 写数据 → +1 恢复稳定态。
 *          Mutex 模式：获取临界区 → 写数据 → 释放临界区。
 * @param pORBHandle      SimpleORB 句柄指针
 * @param data            新数据的缓冲区指针（数据由使用者管理生命周期，发布者仅持有引用）
 * @param length          数据字节长度，必须 >0
 * @return ORB_ERR_T      ORB_ERR_NONE 表示发布成功，ORB_ERR_INVALID_PARAM 表示参数无效
 */
ORB_ERR_T ORBPublish(ORB_HANDLE_T * pORBHandle, void * data, int length);

/**
 * @brief 注册订阅者到 Topic
 * @details 将订阅者节点尾插到 Topic 的 subscriber 链表中，并拷贝当前最新 generation。
 *          阻塞模式（send/receive 非空）：适用于事件驱动场景，订阅者阻塞等待 + 发布者发送邮箱事件。
 *          非阻塞模式（send/receive 均为 NULL）：适用于纯轮询场景。
 * @param pORBHandle                  SimpleORB 句柄指针
 * @param pORBSubscriptionHandle      订阅者句柄指针
 * @param send                        发送回调函数指针（阻塞模式下使用），为空时 receive 也必须为空
 * @param receive                     接收回调函数指针（阻塞模式下使用），为空时 send 也必须为空
 * @return ORB_ERR_T                  ORB_ERR_NONE 表示订阅成功，ORB_ERR_INVALID_PARAM 表示参数无效
 */
ORB_ERR_T ORBSubscribe(ORB_HANDLE_T * pORBHandle, ORB_SUBSCRIPTION_HANDLE_T * pORBSubscriptionHandle, void(*send)(void), void(*receive)(void));

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
ORB_ERR_T ORBCheckNoBlock(ORB_SUBSCRIPTION_HANDLE_T * pORBSubscriptionHandle, bool * updated);

/**
 * @brief 阻塞方式检查数据是否更新
 * @details 调用 receiveMail() 阻塞等待发布者在更新数据时通过 sendMail() 发布邮箱事件。
 * @param pORBSubscriptionHandle    订阅者句柄指针
 * @param updated                   输出参数：true 表示有新数据，false 表示无更新
 * @return ORB_ERR_T                ORB_ERR_NONE 表示检查成功
 * @return ORB_ERR_NO_MAIL          注册订阅时，未配置 receiveMail 回调函数
 */
ORB_ERR_T ORBCheckBlock(ORB_SUBSCRIPTION_HANDLE_T * pORBSubscriptionHandle, bool * updated);

/**
 * @brief 将 Topic 上的最新数据复制到指定缓冲区
 * @details 以原子/互斥方式安全地拷贝一帧完整数据：
 *          - Sequence Lock 模式：Spin-Until-Stable 确认写入完成 → memcpy → 更新本地 generation
 *          - Mutex 模式：获取临界区 → memcpy → 更新本地 generation → 释放临界区
 *          复制完毕后自动更新订阅者的 generation 字段。
 * @param pORBSubscriptionHandle    订阅者句柄指针
 * @param data                      目标缓冲区指针，需由调用者分配并确保容量 >= length
 * @param length                    需要复制的数据字节长度，必须 <= Topic 当前数据的真实长度
 * @return ORB_ERR_T                ORB_ERR_NONE 表示复制成功，ORB_ERR_INVALID_PARAM 表示参数无效
 */
ORB_ERR_T ORBCopy(ORB_SUBSCRIPTION_HANDLE_T * pORBSubscriptionHandle, void * data, int length);

#endif // _SIMPLE_ORB_H_