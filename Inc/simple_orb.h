/**
 ******************************************************************************
 * @file simple_orb.h
 * @author lx
 * @version
 * @date 2026-07-12
 * @brief SimpleOBR - 一款使用发布/订阅模式的精简异步消息中间件
 ******************************************************************************
 * @attention
 *
 ******************************************************************************
 */

#ifndef _SIMPLE_ORB_H_
#define _SIMPLE_ORB_H_

 /* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

typedef uint32_t ORB_GENERATION_T;

/**
 * @brief SimpleORB 返回码定义
 */
typedef enum {
    ORB_ERR_NONE = 0,       /**< 操作成功 */
    ORB_ERR_INVALID_PARAM,  /**< 参数无效 */
    ORB_ERR_NO_MAIL,        /**< 邮箱为空 */
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
 *
 */
typedef struct {
    ORB_SUBSCRIPTION_HANDLE_T * pORBSubscriptionList; /**< 订阅者链表头 */

    ORB_GENERATION_T generation;                       /**< 发布代数，每发布一次递增 */
    void * data;                                       /**< 当前发布的最新数据指针 */
    int length;                                        /**< 当前数据的字节长度 */

    atomic_uint sequenceLock;                          /**< Lock-free 同步用序列锁（偶数=读阶段，奇数=写阶段） */
    ORB_ERR_T(*wait)(void);                           /**< 忙等延迟函数（仅在使用 sequence lock 模式时有效） */
    void(*takeMutexSequenceLock)(void);                /**< P 操作/获取锁回调 */
    void(*giveMutexSequenceLock)(void);                /**< V 操作/释放锁回调 */


    void(*takeMutexORB)(void);                         /**< P 操作/获取锁回调 */
    void(*giveMutexORB)(void);                         /**< V 操作/释放锁回调 */
} ORB_HANDLE_T;

/**
 * @brief 订阅句柄结构体
 *
 * 每个订阅者在调用 ORBSubscribe() 后生成此结构体，用于查询和读取发布的消息
 */
typedef struct {
    struct orb_handle_t * pORBHandle;                   /**< 关联的 SimpleORB Topic 句柄 */
    struct orb_subscription_handle_t * next;            /**< 链表下一个订阅者 */

    ORB_GENERATION_T generation;                        /**< 本机缓存的发布代数，用于检测数据是否更新 */

    void(*sendMail)(void);                              /**< 发送回调：当使用阻塞模式时，在阻塞等待过程中调用 */
    void(*receiveMail)(void);                            /**< 接收回调：当使用非阻塞模式时，在数据更新时调用 */
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
  * @param pORBHandle          SimpleORB 句柄指针
  * @param delay               忙等延迟函数指针（在无锁模式下用于减少 CPU 占用）
  * @return ORB_ERR_T          返回码
  */
ORB_ERR_T ORBInitUseSequenceLock(ORB_HANDLE_T * pORBHandle, void(*wait)(void), void(*take)(void), void(*give)(void));

/**
 * @brief 初始化 SimpleORB 句柄（Mutex 互斥同步模式）
 *
 * @param pORBHandle  SimpleORB 句柄指针
 * @param mutex       Mutex 对象指针
 * @param take        获取锁函数指针（如 osMutexTake / sem_take）
 * @param give        释放锁函数指针（如 osMutexRelease / sem_give）
 * @return ORB_ERR_T  返回码
 */
ORB_ERR_T ORBInitUseMutex(ORB_HANDLE_T * pORBHandle, void(*take)(void), void(*give)(void));

/**
 * @brief 发布数据到 Topic
 *
 * @param pORBHandle  SimpleORB 句柄指针
 * @param data        发布的数据缓冲区指针
 * @param length      数据长度（字节）
 * @return ORB_ERR_T  返回码
 */
ORB_ERR_T ORBPublish(ORB_HANDLE_T * pORBHandle, void * data, int length);

/**
 * @brief 注册订阅者到 Topic
 *
 * @param pORBHandle                    SimpleORB 句柄指针
 * @param pORBSubscriptionHandle        订阅者句柄指针
 * @param mail                          消息缓冲区指针（可为 NULL）
 * @param send                          发送回调函数指针（为 NULL 时需整体设为 NULL）
 * @param receive                       接收回调函数指针（为 NULL 时需整体设为 NULL）
 * @return ORB_ERR_T                    返回码
 *
 * @note mail/send/receive 要么全部非空（阻塞模式），要么全部为空（非阻塞模式）
 */
ORB_ERR_T ORBSubscribe(ORB_HANDLE_T * pORBHandle, ORB_SUBSCRIPTION_HANDLE_T * pORBSubscriptionHandle, void(*send)(void));

/**
 * @brief 非阻塞方式检查数据是否更新
 *
 * @param pORBSubscriptionHandle    订阅者句柄指针
 * @param updated                   输出参数：true 表示有新数据，false 表示无更新
 * @return ORB_ERR_T                返回码
 *
 * @note 仅查看发布代数变化，不获取数据。适用于非阻塞轮询场景
 */
ORB_ERR_T ORBCheckNoBlock(ORB_SUBSCRIPTION_HANDLE_T * pORBSubscriptionHandle, bool * updated);

/**
 * @brief 阻塞方式检查数据是否更新
 *
 * @param pORBSubscriptionHandle    订阅者句柄指针
 * @param updated                   输出参数：true 表示有新数据，false 表示无更新
 * @return ORB_ERR_T                返回码
 *
 * @note 会先调用 user callback receive() 清空旧消息，然后阻塞等待新数据到达。
 *       适用于使用 mail/send/receive 回调的阻塞订阅模式
 */
ORB_ERR_T ORBCheckBlock(ORB_SUBSCRIPTION_HANDLE_T * pORBSubscriptionHandle, bool * updated);

/**
 * @brief 将 Topic 上的最新数据复制到指定缓冲区
 *
 * @param pORBSubscriptionHandle    订阅者句柄指针
 * @param data                      目标缓冲区指针
 * @param length                    需要复制的数据长度（字节）
 * @return ORB_ERR_T                返回码
 *
 * @note 内部自动处理同步，确保复制到的数据是完整的一帧。同时更新订阅者的 generation 字段。
 */
ORB_ERR_T ORBCopy(ORB_SUBSCRIPTION_HANDLE_T * pORBSubscriptionHandle, void * data, int length);

#endif // _SIMPLE_ORB_H_