/**
  ******************************************************************************
  * @file    signal_acquisition.c
  * @brief   两路 ADC 各自循环 DMA，半区完成后复制成稳定帧。
  ******************************************************************************
  */

#include "signal_acquisition.h"
#include <string.h>

/* 两路各一块 DMA 循环缓冲：2048 点，分前后两个半区，每半区正好一帧。 */
static uint16_t adc_dma_buffer[SIGNAL_ACQUISITION_CHANNEL_COUNT]
                              [SIGNAL_ACQUISITION_DMA_SIZE];

/*
 * DMA 每完成一个半区，就把这一半复制到这里。
 * frame_ready=1 时中断不会覆盖它，因此主循环读到的一定是同一帧数据。
 */
static uint16_t adc_stable_frame[SIGNAL_ACQUISITION_CHANNEL_COUNT]
                                [SIGNAL_ACQUISITION_FRAME_SIZE];
static ADC_HandleTypeDef *adc_handle = NULL;
static ADC_HandleTypeDef *feedback_adc_handle = NULL;
static volatile uint8_t channel_ready[SIGNAL_ACQUISITION_CHANNEL_COUNT] = {0U, 0U};
static volatile uint8_t frame_ready = 0U;
static uint32_t sample_rate_hz = 0U;
static uint32_t requested_sample_rate_hz = 0U;

/*
 * 把刚写完的那一半复制进该路的稳定数组。
 * 两路都到齐才置 frame_ready，保证主循环取到的两路来自同一帧。
 */
static void save_completed_frame(uint8_t channel, const uint16_t *source)
{
  /* 上一帧还没被取走时，宁可丢掉本半帧，也不能破坏正在分析的数据。 */
  if (channel >= (uint8_t)SIGNAL_ACQUISITION_CHANNEL_COUNT ||
      source == NULL || frame_ready != 0U)
  {
    return;
  }

  memcpy(adc_stable_frame[channel], source,
         SIGNAL_ACQUISITION_FRAME_SIZE * sizeof(uint16_t));
  channel_ready[channel] = 1U;

  if (channel_ready[SIGNAL_ACQUISITION_PA1] != 0U &&
      channel_ready[SIGNAL_ACQUISITION_DDS_FEEDBACK] != 0U)
  {
    channel_ready[SIGNAL_ACQUISITION_PA1] = 0U;
    channel_ready[SIGNAL_ACQUISITION_DDS_FEEDBACK] = 0U;
    frame_ready = 1U;
  }
}

/* 由 ADC 句柄反查它是第几路，回调里用来决定写哪个稳定数组。 */
static uint8_t channel_of(const ADC_HandleTypeDef *hadc)
{
  if (hadc == adc_handle)
    return (uint8_t)SIGNAL_ACQUISITION_PA1;
  if (hadc == feedback_adc_handle)
    return (uint8_t)SIGNAL_ACQUISITION_DDS_FEEDBACK;
  return (uint8_t)SIGNAL_ACQUISITION_CHANNEL_COUNT;
}

uint8_t SignalAcquisition_SetSampleRate(TIM_HandleTypeDef *htim,
                                        uint32_t timer_clock_hz,
                                        uint32_t requested_rate_hz)
{
  uint32_t total_ticks;
  uint32_t prescaler_ticks;
  uint32_t period_ticks;
  uint64_t period_divisor;

  if (htim == NULL || htim->Instance != TIM3 || adc_handle != NULL ||
      timer_clock_hz == 0U || requested_rate_hz == 0U)
  {
    return 0U;
  }

  /* TIM3 为 16 位定时器，低采样率需要同时使用 PSC 和 ARR 分频。 */
  total_ticks = (uint32_t)(((uint64_t)timer_clock_hz +
                            (requested_rate_hz / 2U)) /
                           requested_rate_hz);
  if (total_ticks == 0U)
    return 0U;

  prescaler_ticks = (uint32_t)(((uint64_t)total_ticks + 65535U) / 65536U);
  if (prescaler_ticks == 0U || prescaler_ticks > 65536U)
    return 0U;

  period_divisor = (uint64_t)requested_rate_hz * prescaler_ticks;
  period_ticks = (uint32_t)(((uint64_t)timer_clock_hz +
                             (period_divisor / 2U)) /
                            period_divisor);
  if (period_ticks == 0U || period_ticks > 65536U)
    return 0U;

  /*
   * 用 HAL 的时基初始化函数重写 PSC / ARR，不自己碰寄存器。
   * HAL_TIM_Base_Init() 内部依次做：
   *   1) 把 htim->Init 里的 Prescaler / Period 写进 TIM3->PSC / TIM3->ARR；
   *   2) 把 URS 置 1，再软发一次 UG —— 新预分频立即搬进工作寄存器、
   *      计数器归零，同时因为 URS=1 不会置出更新标志；
   *   3) 把状态机置回 READY，后面可以正常 HAL_TIM_Base_Start()。
   * 所以这里不需要关定时器、写寄存器、清 CNT、清 UIF。
   */
  htim->Init.Prescaler = prescaler_ticks - 1U;
  htim->Init.Period = period_ticks - 1U;
  if (HAL_TIM_Base_Init(htim) != HAL_OK)
    return 0U;

  requested_sample_rate_hz = requested_rate_hz;
  sample_rate_hz = (uint32_t)((uint64_t)timer_clock_hz /
                              ((uint64_t)prescaler_ticks * period_ticks));
  return 1U;
}

uint32_t SignalAcquisition_GetSampleRate(void)
{
  return sample_rate_hz;
}

uint32_t SignalAcquisition_GetRequestedSampleRate(void)
{
  return requested_sample_rate_hz;
}

uint8_t SignalAcquisition_Start(ADC_HandleTypeDef *master_adc,
                                ADC_HandleTypeDef *slave_adc)
{
  if (master_adc == NULL || slave_adc == NULL)
    return 0U;

  adc_handle = master_adc;
  feedback_adc_handle = slave_adc;
  channel_ready[SIGNAL_ACQUISITION_PA1] = 0U;
  channel_ready[SIGNAL_ACQUISITION_DDS_FEEDBACK] = 0U;
  frame_ready = 0U;

  /*
   * 两路 ADC 都用 TIM3_TRGO 触发，各挂一条 DMA 流，循环写 2048 点。
   * 这里只让通道就绪，调用者最后再启动 TIM3；顺序反了第一次触发会落空。
   */
  if (HAL_ADC_Start_DMA(adc_handle,
                        (uint32_t *)adc_dma_buffer[SIGNAL_ACQUISITION_PA1],
                        SIGNAL_ACQUISITION_DMA_SIZE) != HAL_OK)
  {
    adc_handle = NULL;
    feedback_adc_handle = NULL;
    return 0U;
  }

  if (HAL_ADC_Start_DMA(
          feedback_adc_handle,
          (uint32_t *)adc_dma_buffer[SIGNAL_ACQUISITION_DDS_FEEDBACK],
          SIGNAL_ACQUISITION_DMA_SIZE) != HAL_OK)
  {
    (void)HAL_ADC_Stop_DMA(adc_handle);
    adc_handle = NULL;
    feedback_adc_handle = NULL;
    return 0U;
  }

  return 1U;
}

void SignalAcquisition_Stop(void)
{
  /*
   * 用带 _DMA 后缀的停止函数：它先清 ADON 停转换，再清 CR2.DMA 的
   * DMA 模式位，最后在该流 BUSY 时 abort 掉整条流，一次关完两样。
   * 若改用 HAL_ADC_Stop，只清 ADON，流仍使能，必须再单独 HAL_DMA_Abort。
   */
  if (adc_handle != NULL && adc_handle->DMA_Handle != NULL)
    (void)HAL_ADC_Stop_DMA(adc_handle);
  if (feedback_adc_handle != NULL && feedback_adc_handle->DMA_Handle != NULL)
    (void)HAL_ADC_Stop_DMA(feedback_adc_handle);

  adc_handle = NULL;
  feedback_adc_handle = NULL;
  channel_ready[SIGNAL_ACQUISITION_PA1] = 0U;
  channel_ready[SIGNAL_ACQUISITION_DDS_FEEDBACK] = 0U;
  frame_ready = 0U;
}

const uint16_t *SignalAcquisition_GetFrame(void)
{
  return SignalAcquisition_GetChannelFrame(SIGNAL_ACQUISITION_PA1);
}

const uint16_t *SignalAcquisition_GetChannelFrame(
    SignalAcquisitionChannel channel)
{
  if (frame_ready == 0U || channel >= SIGNAL_ACQUISITION_CHANNEL_COUNT)
    return NULL;

  return adc_stable_frame[channel];
}

void SignalAcquisition_ReleaseFrame(void)
{
  /*
   * 必须等所有使用 adc_stable_frame 的函数都返回后再调用。
   * 清零以后，下一个 DMA 半完成/全完成中断才允许写入新帧。
   */
  frame_ready = 0U;
}

void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
  uint8_t channel;

  /* DMA 写到后半区时，前 1024 点已经完整，可以复制成稳定帧。 */
  channel = channel_of(hadc);
  if (channel < (uint8_t)SIGNAL_ACQUISITION_CHANNEL_COUNT)
    save_completed_frame(channel, &adc_dma_buffer[channel][0]);
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
  uint8_t channel;

  /* DMA 绕回前半区前，后 1024 点已经完整，可以复制成稳定帧。 */
  channel = channel_of(hadc);
  if (channel < (uint8_t)SIGNAL_ACQUISITION_CHANNEL_COUNT)
    save_completed_frame(
        channel, &adc_dma_buffer[channel][SIGNAL_ACQUISITION_FRAME_SIZE]);
}
