/**
  ******************************************************************************
  * @file    signal_acquisition.h
  * @brief   ADC 循环 DMA 采集与稳定帧管理模块。
  *
  * 本模块负责配置 TIM3 采样率，并从 ADC DMA 得到一帧不会被中途覆盖的
  * 1024 点数据；不执行频率识别，也不自动启动 TIM3。
  *
  * 当前工程的调用顺序：
  * @code
  * SignalAcquisition_SetSampleRate(&htim3, 84000000U, 1000000U);  // 内部用 HAL_TIM_Base_Init
  * SignalAcquisition_Start(&hadc1, &hadc2);       // 两路 ADC 与两条 DMA 流就绪
  * HAL_TIM_Base_Start(&htim3);                    // 再启动 TIM3_TRGO 采样触发
  *
  * pa1_frame = SignalAcquisition_GetFrame();
  * fb_frame  = SignalAcquisition_GetChannelFrame(SIGNAL_ACQUISITION_DDS_FEEDBACK);
  * if (pa1_frame != NULL && fb_frame != NULL) {
  *   // 在这里调用频率识别，期间两帧始终保持不变
  *   SignalAcquisition_ReleaseFrame();
  * }
  * @endcode
  ******************************************************************************
  */

#ifndef __SIGNAL_ACQUISITION_H
#define __SIGNAL_ACQUISITION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* 一帧有效采样点数，集成应用会校验其与 FFT 长度一致。 */
#define SIGNAL_ACQUISITION_FRAME_SIZE      1024U
#define SIGNAL_ACQUISITION_CHANNEL_COUNT   2U

typedef enum
{
  SIGNAL_ACQUISITION_PA1 = 0U,
  SIGNAL_ACQUISITION_DDS_FEEDBACK = 1U
} SignalAcquisitionChannel;

/*
 * DMA 缓冲区分成前后两半，每一半正好是一帧。
 * DMA 写后半区时可以处理前半区，写前半区时可以处理后半区。
 */
#define SIGNAL_ACQUISITION_DMA_SIZE        (SIGNAL_ACQUISITION_FRAME_SIZE * 2U)

/**
  * @brief  在 TIM3 停止时配置新的 ADC 触发采样率。
  * @param  htim TIM3 句柄，不能为 NULL。
  * @param  timer_clock_hz TIM3 实际输入时钟，单位 Hz。
  * @param  requested_rate_hz 目标采样率，单位 Hz。
  * @return 1=配置成功，0=参数无效、定时器不兼容或 ADC DMA 仍在运行。
  * @note   函数自动选择 16 位 PSC 和 ARR，并保存由整数分频得到的实际采样率。
  *         写寄存器由 HAL_TIM_Base_Init() 完成：它写 PSC/ARR 并软发一次 UG，
  *         让新分频立即生效、计数器归零，所以调用后第一段间隔就是新采样率。
  *         调用前必须已停止 TIM3、ADC 和 DMA（由 SignalAcquisition_Stop() 保证）。
  */
uint8_t SignalAcquisition_SetSampleRate(TIM_HandleTypeDef *htim,
                                        uint32_t timer_clock_hz,
                                        uint32_t requested_rate_hz);

/** @brief 返回当前整数分频得到的实际采样率，单位 Hz。 */
uint32_t SignalAcquisition_GetSampleRate(void);

/** @brief 返回最近一次成功配置时请求的标称采样率，单位 Hz。 */
uint32_t SignalAcquisition_GetRequestedSampleRate(void);

/**
  * @brief  清空旧数据并启动两路 ADC 的循环 DMA。
  * @param  master_adc PA1 那一路的 ADC 句柄，不能为 NULL。
  * @param  slave_adc  PA2 那一路的 ADC 句柄，不能为 NULL。
  * @return 1=启动成功，0=句柄为空或 HAL_ADC_Start_DMA() 失败。
  * @note   两路 ADC 都配置成 TIM3_TRGO 外部触发，各挂一条循环 DMA 流。
  *         本函数不会启动 TIM3，应由调用者在本函数成功后启动定时器。
  */
uint8_t SignalAcquisition_Start(ADC_HandleTypeDef *master_adc,
                                ADC_HandleTypeDef *slave_adc);

/**
  * @brief  停止当前 ADC DMA，并作废尚未处理的稳定帧。
  * @note   正常持续采样时无需调用；切换采样配置前可以调用。
  */
void SignalAcquisition_Stop(void);

/**
  * @brief  获取最近一帧已经完整复制好的 1024 点 ADC 数据。
  * @return 稳定帧首地址；当前没有新帧时返回 NULL。
  * @note   返回的是模块内部只读数组。调用者不得修改或长期保存该指针，
  *         所有使用者完成读取后必须调用 SignalAcquisition_ReleaseFrame()。
  */
const uint16_t *SignalAcquisition_GetFrame(void);

/** Return the PA1 or PA2 frame captured during the same DMA half-buffer. */
const uint16_t *SignalAcquisition_GetChannelFrame(
    SignalAcquisitionChannel channel);

/**
  * @brief  释放当前稳定帧，允许 DMA 回调保存下一帧。
  * @note   必须等所有读取函数返回后再调用。
  */
void SignalAcquisition_ReleaseFrame(void);

#ifdef __cplusplus
}
#endif

#endif /* __SIGNAL_ACQUISITION_H */
