/**
  ******************************************************************************
  * @file    vision_slave.h
  * @brief   Requirement-5 K230 UART slave for the AD9834 output.
  ******************************************************************************
  */

#ifndef __VISION_SLAVE_H
#define __VISION_SLAVE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

uint8_t VisionSlave_Init(UART_HandleTypeDef *uart);
void VisionSlave_DeInit(void);
uint8_t VisionSlave_Process(void);

#ifdef __cplusplus
}
#endif

#endif /* __VISION_SLAVE_H */

