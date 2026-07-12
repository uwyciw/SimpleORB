/**
 ******************************************************************************
 * @file simple_orb.c
 * @author lx
 * @version
 * @date 2026-07-12
 * @brief SimpleOBR - 一款精简且遵循发布/订阅模式的异步消息中间件
 ******************************************************************************
 * @attention
 *
 ******************************************************************************
 */

 /* Includes ------------------------------------------------------------------*/
#include <string.h>
#include "simple_orb.h"

/********************************************************************************
* 					          Public Functions
********************************************************************************/

/**
  * @brief
  * @param
  * @retval
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

    pORBHandle->sequenceLock = 0;
    pORBHandle->delay = delay;

    pORBHandle->mutex = NULL;
    pORBHandle->take = NULL;
    pORBHandle->give = NULL;

    return ORB_ERR_NONE;
}

/**
  * @brief
  * @param
  * @retval
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

    pORBHandle->sequenceLock = 0;
    pORBHandle->delay = NULL;

    pORBHandle->mutex = mutex;
    pORBHandle->take = take;
    pORBHandle->give = give;

    return ORB_ERR_NONE;
}

/**
  * @brief
  * @param
  * @retval
  */
ORB_ERR_T ORBPublish(ORB_HANDLE_T * pORBHandle, void * data, int length)
{
    if (pORBHandle == NULL || data == NULL || length <= 0) {
        return ORB_ERR_INVALID_PARAM;
    }

    if (pORBHandle->mutex != NULL) {
        pORBHandle->take(pORBHandle->mutex);
    } else {
        pORBHandle->sequenceLock = pORBHandle->sequenceLock + 1;
    }

    pORBHandle->data = data;
    pORBHandle->length = length;
    pORBHandle->generation = pORBHandle->generation + 1;

    if (pORBHandle->mutex != NULL) {
        pORBHandle->give(pORBHandle->mutex);
    } else {
        pORBHandle->sequenceLock = pORBHandle->sequenceLock + 1;
    }

    return ORB_ERR_NONE;
}

/**
  * @brief
  * @param
  * @retval
  */
ORB_ERR_T ORBSubscribe(ORB_HANDLE_T * pORBHandle, ORB_SUBSCRIPTION_HANDLE_T * pORBSubscriptionHandle, void * mail, void(*send)(void *), void(*receive)(void *))
{
    if (pORBHandle == NULL || pORBSubscriptionHandle == NULL || (!((mail == NULL && send == NULL && receive == NULL) || (mail != NULL && send != NULL && receive != NULL)))) {
        return ORB_ERR_INVALID_PARAM;
    }

    pORBSubscriptionHandle->pORBHandle = pORBHandle;
    pORBSubscriptionHandle->mail = mail;
    pORBSubscriptionHandle->send = send;
    pORBSubscriptionHandle->receive = receive;
    pORBSubscriptionHandle->next = NULL;
    pORBSubscriptionHandle->generation = pORBHandle->generation;

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
  * @brief
  * @param
  * @retval
  */
ORB_ERR_T ORBCheckNoBlock(ORB_SUBSCRIPTION_HANDLE_T * pORBSubscriptionHandle, bool * updated)
{
    ORB_GENERATION_T generation;
    uint32_t sequenceLock;

    if (pORBSubscriptionHandle->pORBHandle->mutex != NULL) {
        pORBSubscriptionHandle->pORBHandle->take(pORBSubscriptionHandle->pORBHandle->mutex);
        generation = pORBSubscriptionHandle->pORBHandle->generation;
        pORBSubscriptionHandle->pORBHandle->give(pORBSubscriptionHandle->pORBHandle->mutex);
    } else {
        do {
            while ((pORBSubscriptionHandle->pORBHandle->sequenceLock & 1u) != 0u) {
                pORBSubscriptionHandle->pORBHandle->delay();
            }
            sequenceLock = pORBSubscriptionHandle->pORBHandle->sequenceLock;
            generation = pORBSubscriptionHandle->pORBHandle->generation;
            if (sequenceLock == pORBSubscriptionHandle->pORBHandle->sequenceLock) {
                break;
            }
        } while (1);
    }

    if (pORBSubscriptionHandle->generation != generation) {
        *updated = true;
    } else {
        *updated = false;
    }

    return ORB_ERR_NONE;
}

/**
  * @brief
  * @param
  * @retval
  */
ORB_ERR_T ORBCheckBlock(ORB_SUBSCRIPTION_HANDLE_T * pORBSubscriptionHandle, bool * updated)
{
    ORB_GENERATION_T generation;
    uint32_t sequenceLock;

    if (pORBSubscriptionHandle == NULL || updated == NULL) {
        return ORB_ERR_INVALID_PARAM;
    }

    if (pORBSubscriptionHandle->mail == NULL) {
        return ORB_ERR_NO_MAIL;
    } else {
        pORBSubscriptionHandle->receive(pORBSubscriptionHandle->mail);
    }

    if (pORBSubscriptionHandle->pORBHandle->mutex != NULL) {
        pORBSubscriptionHandle->pORBHandle->take(pORBSubscriptionHandle->pORBHandle->mutex);
        generation = pORBSubscriptionHandle->pORBHandle->generation;
        pORBSubscriptionHandle->pORBHandle->give(pORBSubscriptionHandle->pORBHandle->mutex);
    } else {
        do {
            while ((pORBSubscriptionHandle->pORBHandle->sequenceLock & 1u) != 0u) {
                pORBSubscriptionHandle->pORBHandle->delay();
            }
            sequenceLock = pORBSubscriptionHandle->pORBHandle->sequenceLock;
            generation = pORBSubscriptionHandle->pORBHandle->generation;
            if (sequenceLock == pORBSubscriptionHandle->pORBHandle->sequenceLock) {
                break;
            }
        } while (1);
    }

    if (pORBSubscriptionHandle->generation != generation) {
        *updated = true;
    } else {
        *updated = false;
    }

    return ORB_ERR_NONE;
}

/**
  * @brief
  * @param
  * @retval
  */
ORB_ERR_T ORBCopy(ORB_SUBSCRIPTION_HANDLE_T * pORBSubscriptionHandle, void * data, int length)
{
    uint32_t sequenceLock;

    if (pORBSubscriptionHandle == NULL || data == NULL || length <= 0 || length > pORBSubscriptionHandle->pORBHandle->length) {
        return ORB_ERR_INVALID_PARAM;
    }

    if (pORBSubscriptionHandle->pORBHandle->mutex != NULL) {
        pORBSubscriptionHandle->pORBHandle->take(pORBSubscriptionHandle->pORBHandle->mutex);
        memcpy(data, pORBSubscriptionHandle->pORBHandle->data, length);
        pORBSubscriptionHandle->generation = pORBSubscriptionHandle->pORBHandle->generation;
        pORBSubscriptionHandle->pORBHandle->give(pORBSubscriptionHandle->pORBHandle->mutex);
    } else {
        do {
            while ((pORBSubscriptionHandle->pORBHandle->sequenceLock & 1u) != 0u) {
                pORBSubscriptionHandle->pORBHandle->delay();
            }
            sequenceLock = pORBSubscriptionHandle->pORBHandle->sequenceLock;
            memcpy(data, pORBSubscriptionHandle->pORBHandle->data, length);
            pORBSubscriptionHandle->generation = pORBSubscriptionHandle->pORBHandle->generation;
            if (sequenceLock == pORBSubscriptionHandle->pORBHandle->sequenceLock) {
                break;
            }
        } while (1);
    }

    return ORB_ERR_NONE;
}
