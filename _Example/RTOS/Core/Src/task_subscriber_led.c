/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    task_subscriber_led.c
  * @brief   Subscriber task: lights one LED based on timestamp % 3
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
static osThreadId_t subscriberLEDTaskHandle;
const osThreadAttr_t subscriberLEDTask_attributes = {
  .name = "subscriberLEDTask",
  .priority = (osPriority_t) osPriorityNormal,
  .stack_size = 256 * 4
};

static ORB_SUBSCRIPTION_HANDLE_T subLEDHandle;

/* Private function prototypes -----------------------------------------------*/
static void LEDSendMail(void);
static void LEDReceiveMail(void);

/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

static void LEDSendMail(void)
{
  osThreadFlagsSet(subscriberLEDTaskHandle, 0x1U);
}

static void LEDReceiveMail(void)
{
  osThreadFlagsWait(0x1U, osFlagsWaitAny, osWaitForever);
}

void CreateSubscriberLEDTask(void)
{
  subscriberLEDTaskHandle = osThreadNew(StartSubscriberLEDTask, NULL,
                                        &subscriberLEDTask_attributes);
}

/* USER CODE BEGIN Header_StartSubscriberLEDTask */
/**
* @brief Function implementing the subscriberLEDTask thread.
*       Subscribes to the timestamp topic in blocking mode and
*       lights one of three LEDs based on (timestamp % 3).
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartSubscriberLEDTask */
void StartSubscriberLEDTask(void *argument)
{
  /* USER CODE BEGIN subscriberLEDTask */
  bool updated = false;
  uint32_t timestamp = 0;
  uint32_t result = 0;

  osEventFlagsWait(orbEventId, ORB_FLAG_TOPIC_READY,
                   osFlagsWaitAny | osFlagsNoClear, osWaitForever);

  ORBSubscribeByName(ORB_TOPIC_TIMESTAMP, &subLEDHandle,
                     LEDSendMail, LEDReceiveMail);

  BSP_LED_Off(LED_GREEN);
  BSP_LED_Off(LED_YELLOW);
  BSP_LED_Off(LED_RED);

  for (;;)
  {
    ORBCheckBlock(&subLEDHandle, &updated);
    if (updated == true)
    {
      ORBCopy(&subLEDHandle, &timestamp, sizeof(timestamp));
      result = timestamp % 3U;
      BSP_LED_Off(LED_GREEN);
      BSP_LED_Off(LED_YELLOW);
      BSP_LED_Off(LED_RED);
      switch (result)
      {
        case 0U: BSP_LED_On(LED_GREEN);  break;
        case 1U: BSP_LED_On(LED_YELLOW); break;
        case 2U: BSP_LED_On(LED_RED);    break;
        default: break;
      }
    }
  }
  /* USER CODE END subscriberLEDTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */
