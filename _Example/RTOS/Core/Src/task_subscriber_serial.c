/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    task_subscriber_serial.c
  * @brief   Subscriber task: prints received timestamp via UART
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "orb_topic.h"
#include "simple_orb.h"

/* Private variables ---------------------------------------------------------*/
static osThreadId_t subscriberSerialTaskHandle;
const osThreadAttr_t subscriberSerialTask_attributes = {
  .name = "subscriberSerialTask",
  .priority = (osPriority_t) osPriorityNormal,
  .stack_size = 512 * 4
};

static ORB_SUBSCRIPTION_HANDLE_T subSerialHandle;

/* Private function prototypes -----------------------------------------------*/
static void SerialSendMail(void);
static void SerialReceiveMail(void);

/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

static void SerialSendMail(void)
{
  osThreadFlagsSet(subscriberSerialTaskHandle, 0x1U);
}

static void SerialReceiveMail(void)
{
  osThreadFlagsWait(0x1U, osFlagsWaitAny, osWaitForever);
}

void CreateSubscriberSerialTask(void)
{
  subscriberSerialTaskHandle = osThreadNew(StartSubscriberSerialTask, NULL,
                                           &subscriberSerialTask_attributes);
}

/* USER CODE BEGIN Header_StartSubscriberSerialTask */
/**
* @brief Function implementing the subscriberSerialTask thread.
*       Subscribes to the timestamp topic in blocking mode and
*       prints each received value via COM1 (printf).
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartSubscriberSerialTask */
void StartSubscriberSerialTask(void *argument)
{
  /* USER CODE BEGIN subscriberSerialTask */
  bool updated = false;
  uint32_t timestamp = 0;

  osEventFlagsWait(orbEventId, ORB_FLAG_TOPIC_READY,
                   osFlagsWaitAny | osFlagsNoClear, osWaitForever);

  ORBSubscribeByName(ORB_TOPIC_TIMESTAMP, &subSerialHandle,
                     SerialSendMail, SerialReceiveMail);

  for (;;)
  {
    ORBCheckBlock(&subSerialHandle, &updated);
    if (updated == true)
    {
      ORBCopy(&subSerialHandle, &timestamp, sizeof(timestamp));
      printf("[Serial] timestamp: %lu\r\n", (unsigned long)timestamp);
    }
  }
  /* USER CODE END subscriberSerialTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */
