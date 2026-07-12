/**
 ******************************************************************************
 * @file simple_orb.h
 * @author lx
 * @version
 * @date 2026-07-12
 * @brief SimpleOBR - 一款精简且遵循发布/订阅模式的异步消息中间件
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

typedef enum {
    ORB_ERR_NONE = 0,
    ORB_ERR_INVALID_PARAM,
    ORB_ERR_NO_MAIL,
} ORB_ERR_T;

struct orb_handle_t;
struct orb_subscription_handle_t;

typedef struct {
    struct orb_handle_t * pORBHandle;
    struct orb_subscription_handle_t * next;

    ORB_GENERATION_T generation;

    void * mail;
    void(*send)(void * mail);
    void(*receive)(void * mail);
} ORB_SUBSCRIPTION_HANDLE_T;

typedef struct {
    ORB_SUBSCRIPTION_HANDLE_T * pORBSubscriptionList;

    ORB_GENERATION_T generation;
    void * data;
    int length;

    uint32_t sequenceLock;
    ORB_ERR_T(*delay)(void);

    void * mutex;
    void(*take)(void * mutex);
    void(*give)(void * mutex);
} ORB_HANDLE_T;

ORB_ERR_T ORBInitUseSequenceLock(ORB_HANDLE_T * pORBHandle, void(*delay)(void));
ORB_ERR_T ORBInitUseMutex(ORB_HANDLE_T * pORBHandle, void * mutex, void(*take)(void *), void(*give)(void *));
ORB_ERR_T ORBPublish(ORB_HANDLE_T * pORBHandle, void * data, int length);
ORB_ERR_T ORBSubscribe(ORB_HANDLE_T * pORBHandle, ORB_SUBSCRIPTION_HANDLE_T * pORBSubscriptionHandle, void * mail, void(*send)(void *), void(*receive)(void *));
ORB_ERR_T ORBCheckNoBlock(ORB_SUBSCRIPTION_HANDLE_T * pORBSubscriptionHandle, bool * updated);
ORB_ERR_T ORBCheckBlock(ORB_SUBSCRIPTION_HANDLE_T * pORBSubscriptionHandle, bool * updated);
ORB_ERR_T ORBCopy(ORB_SUBSCRIPTION_HANDLE_T * pORBSubscriptionHandle, void * data, int length);

#endif // _SIMPLE_ORB_H_