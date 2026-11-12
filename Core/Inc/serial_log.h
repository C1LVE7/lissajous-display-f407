/**
  ******************************************************************************
  * @file    serial_log.h
  * @brief   Non-floating-point UART formatting for FFT results.
  ******************************************************************************
  */

#ifndef __SERIAL_LOG_H
#define __SERIAL_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "FFT.h"
#include "dds_pll.h"
#include "main.h"

uint8_t SerialLog_Init(UART_HandleTypeDef *uart);
void SerialLog_DeInit(void);
void SerialLog_PrintStatus(const PeakInfo_t *input_peak,
                           uint32_t input_vpp_mv,
                           const PeakInfo_t *feedback_peak,
                           uint32_t feedback_vpp_mv,
                           const DdsPllState *pll,
                           uint32_t nominal_frequency_millihz);

#ifdef __cplusplus
}
#endif

#endif /* __SERIAL_LOG_H */
