/**
  ******************************************************************************
  * @file    signal_app.c
  * @brief   Minimal F-problem ADC/TIM/DMA to AD9834 signal path.
  ******************************************************************************
  */

#include "signal_app.h"

#include "FFT.h"
#include "ad9834_gpio_f407.h"
#include "app_config.h"
#include "dds_pll.h"
#include "serial_log.h"
#include "signal_acquisition.h"

#if SIGNAL_ACQUISITION_FRAME_SIZE != BUFFER_SIZE
#error "The ADC frame size must match the FFT length"
#endif
#if APP_AUTORANGE_MAX_PEAK_BIN >= (BUFFER_SIZE / 2U)
#error "The upper autorange bin must be inside the positive FFT spectrum"
#endif

static ADC_HandleTypeDef *app_adc = NULL;
static ADC_HandleTypeDef *app_feedback_adc = NULL;
static TIM_HandleTypeDef *app_sample_timer = NULL;
static AD9834_GPIO_HandleTypeDef hdds;
static PeakInfo_t fft_peak;
static PeakInfo_t feedback_fft_peak;
static SignalOutputMode output_mode = SIGNAL_OUTPUT_FUNDAMENTAL;
static int32_t dds_phase_target_mdeg = APP_PLL_DEFAULT_TARGET_MDEG;
static uint8_t dds_ready = 0U;
static uint8_t app_running = 0U;
static uint8_t dds_invalid_frame_count = 0U;
static uint32_t dds_nominal_frequency_millihz = 0U;
static DdsPllState dds_pll;
static uint32_t last_analysis_tick = 0U;
static uint32_t last_range_probe_tick = 0U;
static uint32_t last_uart_tick = 0U;

static uint16_t SignalApp_GetVppCodes(const uint16_t *samples)
{
  uint16_t minimum = 4095U;
  uint16_t maximum = 0U;
  uint32_t i;

  for (i = 0U; i < SIGNAL_ACQUISITION_FRAME_SIZE; i++)
  {
    if (samples[i] < minimum)
      minimum = samples[i];
    if (samples[i] > maximum)
      maximum = samples[i];
  }

  return (uint16_t)(maximum - minimum);
}

static uint32_t SignalApp_LowerSampleRate(uint32_t requested_rate_hz)
{
  uint32_t lower_rate_hz;

  if (requested_rate_hz <= APP_SAMPLE_RATE_MIN_HZ)
    return APP_SAMPLE_RATE_MIN_HZ;

  lower_rate_hz = requested_rate_hz / APP_SAMPLE_RATE_STEP;
  return (lower_rate_hz < APP_SAMPLE_RATE_MIN_HZ) ?
         APP_SAMPLE_RATE_MIN_HZ : lower_rate_hz;
}

static uint32_t SignalApp_ChooseSampleRate(const PeakInfo_t *peak,
                                           uint32_t requested_rate_hz)
{
  if (peak == NULL || peak->valid == 0U)
    return SignalApp_LowerSampleRate(requested_rate_hz);

  if (peak->index < APP_AUTORANGE_MIN_PEAK_BIN &&
      requested_rate_hz > APP_SAMPLE_RATE_MIN_HZ)
  {
    return SignalApp_LowerSampleRate(requested_rate_hz);
  }

  if (peak->index > APP_AUTORANGE_MAX_PEAK_BIN &&
      requested_rate_hz < APP_SAMPLE_RATE_MAX_HZ)
  {
    if (requested_rate_hz >
        (APP_SAMPLE_RATE_MAX_HZ / APP_SAMPLE_RATE_STEP))
    {
      return APP_SAMPLE_RATE_MAX_HZ;
    }
    return requested_rate_hz * APP_SAMPLE_RATE_STEP;
  }

  return requested_rate_hz;
}

static uint8_t SignalApp_ApplySampleRate(uint32_t requested_rate_hz)
{
  if (app_adc == NULL || app_sample_timer == NULL ||
      requested_rate_hz < APP_SAMPLE_RATE_MIN_HZ ||
      requested_rate_hz > APP_SAMPLE_RATE_MAX_HZ)
  {
    return 0U;
  }

  if (HAL_TIM_Base_Stop(app_sample_timer) != HAL_OK)
    return 0U;
  SignalAcquisition_Stop();

  /*
   * 顺序固定：停表 → 改时基（HAL_TIM_Base_Init 会立即装载新分频）
   * → 启两路 ADC/DMA → 开 TIM3。分频一改完就生效，所以启动后第一段就准。
   */
  if (!SignalAcquisition_SetSampleRate(app_sample_timer,
                                       APP_SAMPLE_TIMER_CLOCK_HZ,
                                       requested_rate_hz) ||
      !SignalAcquisition_Start(app_adc, app_feedback_adc))
  {
    return 0U;
  }
  if (HAL_TIM_Base_Start(app_sample_timer) != HAL_OK)
  {
    SignalAcquisition_Stop();
    return 0U;
  }

  last_range_probe_tick = HAL_GetTick();
  DdsPll_Invalidate(&dds_pll);
  return 1U;
}

static uint8_t SignalApp_UpdateDds(const PeakInfo_t *peak)
{
  uint32_t frequency_hz;
  uint32_t frequency_millihz;

  if (!dds_ready)
    return 0U;

  if (peak == NULL || peak->valid == 0U || peak->frequency <= 0.0f)
  {
    if (dds_invalid_frame_count < APP_DDS_INVALID_MUTE_COUNT)
      dds_invalid_frame_count++;
    if (dds_nominal_frequency_millihz != 0U &&
        dds_invalid_frame_count >= APP_DDS_INVALID_MUTE_COUNT)
    {
      (void)AD9834_GPIO_SetSoftwareReset(&hdds, 1U);
      dds_nominal_frequency_millihz = 0U;
      DdsPll_Reset(&dds_pll, 0U);
      return 1U;
    }
    return 0U;
  }

  dds_invalid_frame_count = 0U;

  /* Match the 100 Hz source settings stated in F-problem requirement 3.5. */
  frequency_hz =
      ((uint32_t)(peak->frequency +
                  ((float32_t)APP_INPUT_FREQUENCY_STEP_HZ / 2.0f)) /
       APP_INPUT_FREQUENCY_STEP_HZ) * APP_INPUT_FREQUENCY_STEP_HZ;
  frequency_hz *= (uint32_t)output_mode;

  if (frequency_hz == 0U)
    frequency_hz = APP_INPUT_FREQUENCY_STEP_HZ;
  if (frequency_hz > (hdds.mclk_hz / 2U))
    frequency_hz = hdds.mclk_hz / 2U;

  frequency_millihz = frequency_hz * 1000U;
  if (frequency_millihz != dds_nominal_frequency_millihz &&
      AD9834_GPIO_SetSoftwareReset(&hdds, 1U) == HAL_OK &&
      AD9834_GPIO_WritePhaseWord(&hdds,
                                 AD9834_GPIO_PHASE_REG0,
                                 0U) == HAL_OK &&
      AD9834_GPIO_SetOutputMilliHz(&hdds,
                                   frequency_millihz,
                                   AD9834_GPIO_WAVE_SINE) == HAL_OK)
  {
    dds_nominal_frequency_millihz = frequency_millihz;
    DdsPll_Reset(&dds_pll, frequency_millihz);
    return 1U;
  }

  return 0U;
}

uint8_t SignalApp_Init(ADC_HandleTypeDef *hadc,
                       ADC_HandleTypeDef *feedback_adc,
                       TIM_HandleTypeDef *sample_timer,
                       UART_HandleTypeDef *debug_uart)
{
  if (hadc == NULL || feedback_adc == NULL ||
      sample_timer == NULL || debug_uart == NULL)
    return 0U;

  app_adc = hadc;
  app_feedback_adc = feedback_adc;
  app_sample_timer = sample_timer;
  output_mode = SIGNAL_OUTPUT_FUNDAMENTAL;
  dds_phase_target_mdeg = APP_PLL_DEFAULT_TARGET_MDEG;
  dds_invalid_frame_count = 0U;
  FFT_Init();
  app_running = 0U;
  return SignalApp_Resume(debug_uart);
}

uint8_t SignalApp_Suspend(void)
{
  uint8_t ok = 1U;

  if (app_running == 0U)
    return 1U;

  if (HAL_TIM_Base_Stop(app_sample_timer) != HAL_OK)
    ok = 0U;
  SignalAcquisition_Stop();
  if (dds_ready != 0U &&
      AD9834_GPIO_SetSoftwareReset(&hdds, 1U) != HAL_OK)
  {
    ok = 0U;
  }
  SerialLog_DeInit();
  dds_ready = 0U;
  dds_invalid_frame_count = 0U;
  dds_nominal_frequency_millihz = 0U;
  DdsPll_Reset(&dds_pll, 0U);
  app_running = 0U;
  return ok;
}

uint8_t SignalApp_Resume(UART_HandleTypeDef *debug_uart)
{
  if (app_adc == NULL || app_feedback_adc == NULL ||
      app_sample_timer == NULL || debug_uart == NULL)
  {
    return 0U;
  }
  if (app_running != 0U)
    return 1U;
  if (!SerialLog_Init(debug_uart))
    return 0U;

  dds_ready = (AD9834_GPIO_InitDefault(&hdds) == HAL_OK) ? 1U : 0U;
  if (!dds_ready ||
      AD9834_GPIO_SetSoftwareReset(&hdds, 1U) != HAL_OK)
  {
    dds_ready = 0U;
    SerialLog_DeInit();
    return 0U;
  }

  /* Keep the DDS muted until a valid FFT peak selects the sine frequency. */
  dds_invalid_frame_count = 0U;
  dds_nominal_frequency_millihz = 0U;
  DdsPll_Reset(&dds_pll, 0U);
  last_analysis_tick = HAL_GetTick();
  last_uart_tick = HAL_GetTick();
  if (!SignalApp_ApplySampleRate(APP_SAMPLE_RATE_MAX_HZ))
  {
    (void)AD9834_GPIO_SetSoftwareReset(&hdds, 1U);
    dds_ready = 0U;
    SerialLog_DeInit();
    return 0U;
  }
  app_running = 1U;
  return 1U;
}

uint8_t SignalApp_SetOutputMode(SignalOutputMode mode)
{
  if (mode != SIGNAL_OUTPUT_FUNDAMENTAL &&
      mode != SIGNAL_OUTPUT_DOUBLE)
  {
    return 0U;
  }

  output_mode = mode;
  dds_invalid_frame_count = 0U;
  /* 名义频率清 0 逼下一分析节拍必定重写频点，这里不再当场写 DDS。 */
  dds_nominal_frequency_millihz = 0U;
  DdsPll_Reset(&dds_pll, 0U);
  return 1U;
}

uint8_t SignalApp_ToggleOutputMode(void)
{
  return SignalApp_SetOutputMode(
      (output_mode == SIGNAL_OUTPUT_FUNDAMENTAL) ?
      SIGNAL_OUTPUT_DOUBLE : SIGNAL_OUTPUT_FUNDAMENTAL);
}

uint8_t SignalApp_TogglePhaseTarget(void)
{
  /* 只改这里，DdsPll_Update 入口每帧从参数同步到状态结构体。 */
  dds_phase_target_mdeg =
      (dds_phase_target_mdeg == APP_PLL_DEFAULT_TARGET_MDEG) ?
      APP_PLL_ALTERNATE_TARGET_MDEG : APP_PLL_DEFAULT_TARGET_MDEG;
  DdsPll_Invalidate(&dds_pll);
  return 1U;
}

uint8_t SignalApp_Process(void)
{
  const uint16_t *adc_frame;
  const uint16_t *feedback_frame;
  uint32_t now;

  if (app_running == 0U || app_adc == NULL || app_sample_timer == NULL)
    return 0U;

  adc_frame = SignalAcquisition_GetFrame();
  if (adc_frame == NULL)
    return 1U;
  feedback_frame = SignalAcquisition_GetChannelFrame(
      SIGNAL_ACQUISITION_DDS_FEEDBACK);
  if (feedback_frame == NULL)
  {
    SignalAcquisition_ReleaseFrame();
    return 1U;
  }

  now = HAL_GetTick();
  if ((uint32_t)(now - last_analysis_tick) >= APP_ANALYSIS_INTERVAL_MS)
  {
    uint32_t sample_rate_hz = SignalAcquisition_GetSampleRate();
    uint32_t requested_rate_hz =
        SignalAcquisition_GetRequestedSampleRate();
    uint32_t next_sample_rate_hz;
    uint32_t phase_window_size;
    uint16_t input_vpp_codes;
    uint16_t feedback_vpp_codes;
    uint32_t input_vpp_mv;
    uint32_t feedback_vpp_mv;
    float32_t phase_reference_frequency_hz;
    uint8_t fft_ok;
    uint8_t feedback_fft_ok;

    last_analysis_tick = now;
    input_vpp_codes = SignalApp_GetVppCodes(adc_frame);
    fft_ok = FFT_DetectFrequency(adc_frame,
                                 SIGNAL_ACQUISITION_FRAME_SIZE,
                                 (float32_t)sample_rate_hz,
                                 &fft_peak);
    /* 前端 1:1 直连，引脚峰峰值就是实际电压，无需折算。 */
    input_vpp_mv = fft_ok ? fft_peak.peak_to_peak_mv : 0U;
    if (!fft_ok ||
        input_vpp_codes < APP_INPUT_MIN_VPP_ADC_CODES ||
        fft_peak.frequency <
            ((float32_t)APP_INPUT_FREQUENCY_MIN_HZ -
             (float32_t)APP_INPUT_FREQUENCY_STEP_HZ * 0.5f) ||
        fft_peak.frequency >
            ((float32_t)APP_INPUT_FREQUENCY_MAX_HZ +
             (float32_t)APP_INPUT_FREQUENCY_STEP_HZ * 0.5f))
    {
      fft_peak.valid = 0U;
    }

    feedback_vpp_codes = SignalApp_GetVppCodes(feedback_frame);
    feedback_fft_ok = FFT_DetectFrequency(feedback_frame,
                                          SIGNAL_ACQUISITION_FRAME_SIZE,
                                          (float32_t)sample_rate_hz,
                                          &feedback_fft_peak);
    feedback_vpp_mv = feedback_fft_ok ?
        feedback_fft_peak.peak_to_peak_mv : 0U;
    if (!feedback_fft_ok ||
        feedback_vpp_codes < APP_INPUT_MIN_VPP_ADC_CODES)
    {
      feedback_fft_peak.valid = 0U;
    }

    if (fft_peak.valid != 0U && feedback_fft_peak.valid != 0U)
    {
      phase_reference_frequency_hz = fft_peak.frequency;
      phase_window_size = FFT_GetPhaseWindowSize(
          SIGNAL_ACQUISITION_FRAME_SIZE,
          (float32_t)sample_rate_hz,
          phase_reference_frequency_hz);
      if (phase_window_size == 0U ||
          !FFT_EstimatePhaseAtFrequency(
              adc_frame,
              SIGNAL_ACQUISITION_FRAME_SIZE,
              (float32_t)sample_rate_hz,
              phase_reference_frequency_hz,
              phase_window_size,
              &fft_peak.phase_radians) ||
          !FFT_EstimatePhaseAtFrequency(
              feedback_frame,
              SIGNAL_ACQUISITION_FRAME_SIZE,
              (float32_t)sample_rate_hz,
              phase_reference_frequency_hz * (float32_t)output_mode,
              phase_window_size,
              &feedback_fft_peak.phase_radians))
      {
        fft_peak.valid = 0U;
        feedback_fft_peak.valid = 0U;
      }
    }

    next_sample_rate_hz =
        SignalApp_ChooseSampleRate(&fft_peak, requested_rate_hz);
    if (next_sample_rate_hz != requested_rate_hz)
    {
      SignalAcquisition_ReleaseFrame();
      return SignalApp_ApplySampleRate(next_sample_rate_hz);
    }

    if (!SignalApp_UpdateDds(&fft_peak) &&
        dds_nominal_frequency_millihz != 0U)
    {
      if (!DdsPll_Update(&dds_pll,
                         &hdds,
                         &fft_peak,
                         &feedback_fft_peak,
                         (uint8_t)output_mode,
                         dds_nominal_frequency_millihz,
                         dds_phase_target_mdeg))
      {
        SignalAcquisition_ReleaseFrame();
        return 0U;
      }
    }

    if ((uint32_t)(now - last_uart_tick) >= APP_UART_PRINT_INTERVAL_MS)
    {
      last_uart_tick = now;
      SerialLog_PrintStatus(&fft_peak, input_vpp_mv,
                            &feedback_fft_peak, feedback_vpp_mv,
                            &dds_pll, dds_nominal_frequency_millihz);
    }
  }

  SignalAcquisition_ReleaseFrame();

  if ((uint32_t)(HAL_GetTick() - last_range_probe_tick) >=
          APP_AUTORANGE_RECHECK_MS &&
      SignalAcquisition_GetRequestedSampleRate() != APP_SAMPLE_RATE_MAX_HZ)
  {
    return SignalApp_ApplySampleRate(APP_SAMPLE_RATE_MAX_HZ);
  }

  return 1U;
}
