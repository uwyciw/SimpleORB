/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    task_publisher.c
  * @brief   Publisher task: publishes timestamp on button press
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
const osThreadAttr_t publisherTask_attributes = {
  .name = "publisherTask",
  .priority = (osPriority_t) osPriorityNormal,
  .stack_size = 256 * 4
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

void CreatePublisherTask(void)
{
  osThreadNew(StartPublisherTask, NULL, &publisherTask_attributes);
}

/* USER CODE BEGIN Header_StartPublisherTask */
/**
* @brief Function implementing the publisherTask thread.
*       Waits for topic to be created, then publishes the current
*       system timestamp whenever the USER button is pressed.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartPublisherTask */
void StartPublisherTask(void *argument)
{
  /* USER CODE BEGIN publisherTask */

  osEventFlagsWait(orbEventId, ORB_FLAG_TOPIC_READY,
                   osFlagsWaitAny | osFlagsNoClear, osWaitForever);

  for (;;)
  {
    if (BspButtonState == BUTTON_PRESSED)
    {
      BspButtonState = BUTTON_RELEASED;
      uint32_t publishBuffer = xTaskGetTickCount() ;
      ORBPublishByName(ORB_TOPIC_TIMESTAMP, &publishBuffer, sizeof(publishBuffer));
    }
    osDelay(10);
  }
  /* USER CODE END publisherTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */
