/**
  ******************************************************************************
  * @file    signal_app.h
  * @brief   ADC/TIM/DMA acquisition and DDS output scheduler.
  ******************************************************************************
  */

#ifndef __SIGNAL_APP_H
#define __SIGNAL_APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

typedef enum
{
  SIGNAL_OUTPUT_FUNDAMENTAL = 1U,
  SIGNAL_OUTPUT_DOUBLE = 2U
} SignalOutputMode;

uint8_t SignalApp_Init(ADC_HandleTypeDef *hadc,
                       ADC_HandleTypeDef *feedback_adc,
                       TIM_HandleTypeDef *sample_timer,
                       UART_HandleTypeDef *debug_uart);

/** Stop normal ADC/DMA processing and mute the DDS for another application. */
uint8_t SignalApp_Suspend(void);

/** Restart the normal acquisition and DDS controller after suspension. */
uint8_t SignalApp_Resume(UART_HandleTypeDef *debug_uart);

uint8_t SignalApp_Process(void);

/** Select same-frequency or double-frequency DDS output. */
uint8_t SignalApp_SetOutputMode(SignalOutputMode mode);

/** Toggle between same-frequency and double-frequency DDS output. */
uint8_t SignalApp_ToggleOutputMode(void);

/** Toggle the DDS phase target between 0 and +90 degrees. */
uint8_t SignalApp_TogglePhaseTarget(void);

#ifdef __cplusplus
}
#endif

#endif /* __SIGNAL_APP_H */
