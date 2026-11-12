/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define QUESTION_SELECT_BUTTON_Pin GPIO_PIN_0
#define QUESTION_SELECT_BUTTON_GPIO_Port GPIOA
#define DDS_PHASE_BUTTON_Pin GPIO_PIN_2
#define DDS_PHASE_BUTTON_GPIO_Port GPIOE
#define DDS_DOUBLE_BUTTON_Pin GPIO_PIN_3
#define DDS_DOUBLE_BUTTON_GPIO_Port GPIOE
#define OUTPUT_SELECT_BUTTON_Pin GPIO_PIN_4
#define OUTPUT_SELECT_BUTTON_GPIO_Port GPIOE
#define SELECT_OUTPUT_PB4_Pin GPIO_PIN_4
#define SELECT_OUTPUT_PB4_GPIO_Port GPIOB
#define SELECT_OUTPUT_PB5_Pin GPIO_PIN_5
#define SELECT_OUTPUT_PB5_GPIO_Port GPIOB
#define SELECT_OUTPUT_PB6_Pin GPIO_PIN_6
#define SELECT_OUTPUT_PB6_GPIO_Port GPIOB
#define SELECT_OUTPUT_PB7_Pin GPIO_PIN_7
#define SELECT_OUTPUT_PB7_GPIO_Port GPIOB
#define VISION_LINE_BUTTON_Pin GPIO_PIN_12
#define VISION_LINE_BUTTON_GPIO_Port GPIOD
#define VISION_CIRCLE_BUTTON_Pin GPIO_PIN_13
#define VISION_CIRCLE_BUTTON_GPIO_Port GPIOD
#define VISION_INFINITY_BUTTON_Pin GPIO_PIN_3
#define VISION_INFINITY_BUTTON_GPIO_Port GPIOD
#define VISION_BUZZER_Pin GPIO_PIN_0
#define VISION_BUZZER_GPIO_Port GPIOE
#define VISION_DONE_Pin GPIO_PIN_8
#define VISION_DONE_GPIO_Port GPIOA

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
