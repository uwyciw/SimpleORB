/**
  ******************************************************************************
  * @file    simple_orb.h
  * @author  lx
  * @version V1.0.0
  * @date    2026-07-12
  * @brief   -SimpleORB - 一款基于发布/订阅模式的轻量级异步消息中间件
  *          -支持 SequenceLock 和 Mutex 两种临界区保护方案，SequenceLock 方案适合裸机环境或写少读多场景使用，Mutex 方案适合在有 RTOS 且读写均衡场景使用
  ******************************************************************************
  * @attention
  * ORB_HANDLE_T 需要被定义为全局或静态变量
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
 *        使用者需通过 __ORB_HANDLE_ALLOC() 宏分配此结构体的实例，并需要定义为全局或静态变量。
 */
typedef struct orb_handle_t{
    struct orb_subscription_handle_t * pORBSubscriptionList;   /**< 订阅者链表的头节点指针 */

    unsigned int generation;                            /**< ORB 更新代数，循环计数 */
    void * data;                                        /**< 最近一次发布的数据缓冲区指针，由发布者提供，不持有所有权 */
    int length;                                         /**< 最近一次发布的数据字节长度 */

    /* --- Sequence Lock --- */
    atomic_uint sequenceLock;                           /**< 原子序列锁：偶数值表示"可读稳定态"，奇数值表示"写入进行中" */
    void(*wait)(void);                             /**< 忙等延迟函数指针，仅在 Sequence Lock 模式下有效，用于空转时降低 CPU 占用，如初始化为NULL则不使用 */
    void(*takeMutexSequenceLock)(void);                 /**< 序列锁模式的 P 操作（获取临界区），仅在publish时使用，如不需要可以设置为NULL */
    void(*giveMutexSequenceLock)(void);                 /**< 序列锁模式的 V 操作（释放临界区），仅在publish时使用, 如不需要可以设置为NULL */

    /* --- Mutex --- */
    void(*takeMutexORB)(void);                          /**< Mutex 模式的 P 操作（获取临界区）, publish 和 copy 时使用 */
    void(*giveMutexORB)(void);                          /**< Mutex 模式的 V 操作（释放临界区）, publish 和 copy 时使用 */
} ORB_HANDLE_T;

/**
 * @brief 订阅句柄结构体
 */
typedef struct orb_subscription_handle_t{
    struct orb_handle_t * pORBHandle;                   /**< 反向引用：指向关联 Topic 的 ORB_HANDLE_T 句柄 */
    struct orb_subscription_handle_t * next;            /**< 单向链表后继节点指针，由内部链表管理，用户无需修改 */

    unsigned int generation;                            /**< 本地缓存的生成号：记录上一次已处理的数据版本，用于增量检测 */

    void(*sendMail)(void);                              /**< 发送回调：阻塞模式下，在等待新数据之前回调（常用于任务发信号/投递消息） */
    void(*receiveMail)(void);                           /**< 接收回调：阻塞模式下，在检测到数据更新前回调（常用于清空旧缓冲/重置状态） */
} ORB_SUBSCRIPTION_HANDLE_T;

/**
 * @brief 分配 SimpleORB 句柄的宏
 *
 * @param __NAME__  句柄变量名
 *
 * 示例: __ORB_HANDLE_ALLOC(myTopic) 等价于定义 ORB_HANDLE_T myTopic = {.sequenceLock = ATOMIC_VAR_INIT(0)}
 */
#define __ORB_HANDLE_ALLOC(__NAME__) ORB_HANDLE_T __NAME__ = {.sequenceLock = ATOMIC_VAR_INIT(0)}

 /**
  * @brief 初始化 SimpleORB 句柄（Sequence Lock 无锁同步模式）
  *
  * @details 使用 C11 <stdatomic.h> 原子变量实现 Sequence Lock 机制。
  *          写入时先将 sequenceLock+1（奇数），读取时等待偶数后校验一致性。
  *          take/give 回调可选为 NULL，此时仅依赖原子操作完成同步。
  *
  * @param pORBHandle      SimpleORB 句柄指针
  * @param wait            忙等延迟函数指针，为空则空转；仅在 Sequence Lock 模式下有效
  * @param take            获取临界区资源回调，与 give 要么同时为 NULL，要么同时非 NULL
  * @param give            释放临界区资源回调，与 take 要么同时为 NULL，要么同时非 NULL
  * @return ORB_ERR_T              ORB_ERR_NONE 表示初始化成功，ORB_ERR_INVALID_PARAM 表示参数无效
  */
ORB_ERR_T ORBInitUseSequenceLock(ORB_HANDLE_T * pORBHandle, void(*wait)(void), void(*take)(void), void(*give)(void));

/**
 * @brief 初始化 SimpleORB 句柄（Mutex 互斥同步模式）
 *
 * @details 使用用户提供的 Mutex P/V 操作回调实现全链路临界区保护。
 *          适用于不支持 C11 原子操作的平台，或需要 OS 级调度支持的场景。
 *
 * @param pORBHandle      SimpleORB 句柄指针
 * @param take            获取 Mutex 的 P 操作回调（如 osMutexTake、sem_wait），不可为 NULL
 * @param give            释放 Mutex 的 V 操作回调（如 osMutexRelease、sem_post），不可为 NULL
 * @return ORB_ERR_T      ORB_ERR_NONE 表示初始化成功，ORB_ERR_INVALID_PARAM 表示参数无效
 */
ORB_ERR_T ORBInitUseMutex(ORB_HANDLE_T * pORBHandle, void(*take)(void), void(*give)(void));

/**
 * @brief 发布数据到 Topic
 *
 * @details 将 data 和 length 写入共享状态，递增 generation。
 *          完成后遍历所有订阅者，调用各自的 sendMail() 回调。
 *          Sequence Lock 模式：+1 进入"写入中"态 → 写数据 → +1 恢复稳定态。
 *          Mutex 模式：获取临界区 → 写数据 → 释放临界区。
 *
 * @param pORBHandle      SimpleORB 句柄指针
 * @param data            新数据的缓冲区指针（数据由使用者管理生命周期，发布者仅持有引用）
 * @param length          数据字节长度，必须 >0
 * @return ORB_ERR_T      ORB_ERR_NONE 表示发布成功，ORB_ERR_INVALID_PARAM 表示参数无效
 */
ORB_ERR_T ORBPublish(ORB_HANDLE_T * pORBHandle, void * data, int length);

/**
 * @brief 注册订阅者到 Topic
 *
 * @details 将订阅者节点尾插到 Topic 的 subscriber 链表中，并拷贝当前最新 generation。
 *          阻塞模式（send/receive 非空）：适用于事件驱动场景，发布者通知 + 等待回调配合轮询。
 *          非阻塞模式（send/receive 均为 NULL）：适用于纯轮询场景，无需外部回调。
 *
 * @param pORBHandle                  SimpleORB 句柄指针
 * @param pORBSubscriptionHandle      订阅者句柄指针（由用户分配并传入）
 * @param send                        发送回调函数指针（阻塞模式下使用），为空时 receive 也必须为空
 * @param receive                     接收回调函数指针（阻塞模式下使用），为空时 send 也必须为空
 * @return ORB_ERR_T                  ORB_ERR_NONE 表示订阅成功，ORB_ERR_INVALID_PARAM 表示参数无效
 *
 * @note 两个回调要么同时为 NULL（非阻塞模式），要么同时非 NULL（阻塞模式）。
 */
ORB_ERR_T ORBSubscribe(ORB_HANDLE_T * pORBHandle, ORB_SUBSCRIPTION_HANDLE_T * pORBSubscriptionHandle, void(*send)(void), void(*receive)(void));

/**
 * @brief 非阻塞方式检查数据是否更新
 *
 * @details 读取当前 Topic 的 generation 并与订阅者缓存值比较。
 *          Sequence Lock 模式下：等到稳定态 → 读 generation → Spin Until Stable 验证一致性。
 *          Mutex 模式下：加锁 → 读 generation → 解锁。
 *          仅做增量检测，不拷贝数据。
 *
 * @param pORBSubscriptionHandle    订阅者句柄指针
 * @param updated                   输出参数：true 表示有新数据待处理，false 表示与上次一致无变化
 * @return ORB_ERR_T                ORB_ERR_NONE 表示检查成功
 *
 * @note 适用于轮询场景。调用方在获得 true 后应进一步调用 ORBCopy() 获取实际数据。
 */
ORB_ERR_T ORBCheckNoBlock(ORB_SUBSCRIPTION_HANDLE_T * pORBSubscriptionHandle, bool * updated);

/**
 * @brief 阻塞方式检查数据是否更新
 *
 * @details 先调用 receiveMail() 回调清空旧消息状态，然后以与非阻塞模式相同的同步原语检测 generation 变化。
 *          Sequence Lock 模式：Spin-Until-Stable → 比较代数；Mutex 模式：加锁 → 读数 → 解锁。
 *          适用于带有 Mail/事件机制的场景：用户回调负责收发逻辑，此函数仅检测更新。
 *
 * @param pORBSubscriptionHandle    订阅者句柄指针
 * @param updated                   输出参数：true 表示有新数据，false 表示无更新
 * @return ORB_ERR_T                ORB_ERR_NONE 表示检查成功
 *
 * @retval ORB_ERR_NO_MAIL      未配置 receiveMail 回调（即非阻塞模式）时调用此函数
 * @note 仅在阻塞模式（send/receive 非空）下使用；若以非阻塞方式注册了该订阅，将返回错误。
 */
ORB_ERR_T ORBCheckBlock(ORB_SUBSCRIPTION_HANDLE_T * pORBSubscriptionHandle, bool * updated);

/**
 * @brief 将 Topic 上的最新数据复制到指定缓冲区
 *
 * @details 以原子/互斥方式安全地拷贝一帧完整数据：
 *          - Sequence Lock 模式：Spin-Until-Stable 确认写入完成 → memcpy → 更新本地 generation
 *          - Mutex 模式：获取临界区 → memcpy → 更新本地 generation → 释放临界区
 *          复制完毕后自动更新订阅者的 generation 字段，防止重复消费同一帧。
 *
 * @param pORBSubscriptionHandle    订阅者句柄指针
 * @param data                      目标缓冲区指针，需由调用者分配并确保容量 >= length
 * @param length                    需要复制的数据字节长度，必须 <= Topic 当前数据的真实长度
 * @return ORB_ERR_T                ORB_ERR_NONE 表示复制成功，ORB_ERR_INVALID_PARAM 表示参数无效
 *
 * @note 内部保证拷贝到的数据一定是某一次完整 ORBPublish() 的结果，不会出现半帧读取。
 */
ORB_ERR_T ORBCopy(ORB_SUBSCRIPTION_HANDLE_T * pORBSubscriptionHandle, void * data, int length);

#endif // _SIMPLE_ORB_H_