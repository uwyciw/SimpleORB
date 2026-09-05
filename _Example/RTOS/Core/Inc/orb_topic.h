/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    orb_topic.h
  * @brief   SimpleORB RTOS example shared definitions
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __ORB_TOPIC_H
#define __ORB_TOPIC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_freertos.h"

#define ORB_TOPIC_TIMESTAMP   "timestamp"
#define ORB_FLAG_TOPIC_READY  (1U << 0)

extern osEventFlagsId_t orbEventId;

extern __IO uint32_t BspButtonState;

void CreatePublisherTask(void);
void CreateSubscriberSerialTask(void);
void CreateSubscriberLEDTask(void);

void StartPublisherTask(void *argument);
void StartSubscriberSerialTask(void *argument);
void StartSubscriberLEDTask(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* __ORB_TOPIC_H */
