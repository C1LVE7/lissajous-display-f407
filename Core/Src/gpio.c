/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gpio.c
  * @brief   This file provides code for the configuration
  *          of all used GPIO pins.
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

/* Includes ------------------------------------------------------------------*/
#include "gpio.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/*----------------------------------------------------------------------------*/
/* Configure GPIO                                                             */
/*----------------------------------------------------------------------------*/
/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/** Configure pins as
        * Analog
        * Input
        * Output
        * EVENT_OUT
        * EXTI
*/
void MX_GPIO_Init(void)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, SELECT_OUTPUT_PB5_Pin|SELECT_OUTPUT_PB6_Pin|SELECT_OUTPUT_PB7_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, SELECT_OUTPUT_PB4_Pin, GPIO_PIN_RESET);

  /* Vision completion outputs are active high and idle low. */
  HAL_GPIO_WritePin(VISION_BUZZER_GPIO_Port, VISION_BUZZER_Pin,
                    GPIO_PIN_RESET);
  HAL_GPIO_WritePin(VISION_DONE_GPIO_Port, VISION_DONE_Pin,
                    GPIO_PIN_RESET);

  /*Configure GPIO pin : QUESTION_SELECT_BUTTON_Pin */
  GPIO_InitStruct.Pin = QUESTION_SELECT_BUTTON_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(QUESTION_SELECT_BUTTON_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : DDS_DOUBLE_BUTTON_Pin */
  GPIO_InitStruct.Pin = DDS_DOUBLE_BUTTON_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(DDS_DOUBLE_BUTTON_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : DDS_PHASE_BUTTON_Pin */
  GPIO_InitStruct.Pin = DDS_PHASE_BUTTON_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(DDS_PHASE_BUTTON_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : OUTPUT_SELECT_BUTTON_Pin */
  GPIO_InitStruct.Pin = OUTPUT_SELECT_BUTTON_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(OUTPUT_SELECT_BUTTON_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : SELECT_OUTPUT_PB4_Pin SELECT_OUTPUT_PB5_Pin SELECT_OUTPUT_PB6_Pin SELECT_OUTPUT_PB7_Pin */
  GPIO_InitStruct.Pin = SELECT_OUTPUT_PB4_Pin|SELECT_OUTPUT_PB5_Pin|SELECT_OUTPUT_PB6_Pin|SELECT_OUTPUT_PB7_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* Requirement-5 external buttons: PD12=LINE, PD13=CIRCLE, PD3=INFINITY. */
  GPIO_InitStruct.Pin = VISION_LINE_BUTTON_Pin |
                        VISION_CIRCLE_BUTTON_Pin |
                        VISION_INFINITY_BUTTON_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /* PE0 drives the on-board active buzzer through its S8050 transistor. */
  GPIO_InitStruct.Pin = VISION_BUZZER_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(VISION_BUZZER_GPIO_Port, &GPIO_InitStruct);

  /* PA8 is exposed as P1 pin 28 and mirrors the completion pulse. */
  GPIO_InitStruct.Pin = VISION_DONE_Pin;
  HAL_GPIO_Init(VISION_DONE_GPIO_Port, &GPIO_InitStruct);

}

/* USER CODE BEGIN 2 */

/* USER CODE END 2 */
