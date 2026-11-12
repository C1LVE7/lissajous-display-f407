/**
  ******************************************************************************
  * @file    FFT.c
  * @brief   Hann-windowed frequency detector for the F-problem input signal.
  ******************************************************************************
  */

#include "FFT.h"

#include "app_config.h"
#include <math.h>

extern const float32_t twiddleCoef_1024[2048];
extern const uint16_t armBitRevIndexTable1024[1800];

#if BUFFER_SIZE != 1024U
#error "The selected CMSIS-DSP FFT instance requires 1024 samples"
#endif

#define FFT_POSITIVE_BIN_COUNT ((BUFFER_SIZE / 2U) + 1U)
#define FFT_PHASE_WINDOW_MIN_SIZE  256U
#define FFT_PHASE_WINDOW_MID_SIZE  512U
#define FFT_PHASE_WINDOW_HIGH_BIN   32.0f
#define FFT_PHASE_WINDOW_MID_BIN    16.0f

#if FFT_PHASE_WINDOW_MIN_SIZE == 0U || \
    FFT_PHASE_WINDOW_MID_SIZE > BUFFER_SIZE || \
    FFT_PHASE_WINDOW_MIN_SIZE > FFT_PHASE_WINDOW_MID_SIZE
#error "The phase estimation window must fit inside the FFT frame"
#endif

static const arm_cfft_instance_f32 fft_instance = {
  BUFFER_SIZE,
  twiddleCoef_1024,
  armBitRevIndexTable1024,
  1800U
};

static float32_t fft_buffer[BUFFER_SIZE * 2U];
static float32_t magnitude_buffer[FFT_POSITIVE_BIN_COUNT];
static float32_t hann_window[BUFFER_SIZE];
static uint8_t fft_initialized = 0U;

static uint32_t FFT_EstimatePeakToPeakMv(const uint16_t *adc_buffer,
                                         uint32_t size,
                                         float32_t dc_offset,
                                         float32_t fractional_bin)
{
  float32_t step_angle = 2.0f * PI * fractional_bin /
                         (float32_t)BUFFER_SIZE;
  float32_t step_cosine = arm_cos_f32(step_angle);
  float32_t step_sine = arm_sin_f32(step_angle);
  float32_t cosine_value = 1.0f;
  float32_t sine_value = 0.0f;
  float32_t in_phase = 0.0f;
  float32_t quadrature = 0.0f;
  float32_t used_window_sum = 0.0f;
  float32_t vector_magnitude;
  float32_t peak_to_peak_mv;
  uint32_t i;

  /* Fit one sine at the interpolated FFT frequency using the same Hann window. */
  for (i = 0U; i < size; i++)
  {
    float32_t weighted_sample =
        ((float32_t)adc_buffer[i] - dc_offset) * hann_window[i];
    float32_t next_cosine;
    float32_t next_sine;

    in_phase += weighted_sample * cosine_value;
    quadrature -= weighted_sample * sine_value;
    used_window_sum += hann_window[i];

    next_cosine = cosine_value * step_cosine - sine_value * step_sine;
    next_sine = sine_value * step_cosine + cosine_value * step_sine;
    cosine_value = next_cosine;
    sine_value = next_sine;
  }

  if (used_window_sum <= 0.0f ||
      arm_sqrt_f32(in_phase * in_phase + quadrature * quadrature,
                   &vector_magnitude) != ARM_MATH_SUCCESS)
  {
    return 0U;
  }

  /* 2/window_sum gives sine peak amplitude; multiply by two again for Vpp. */
  peak_to_peak_mv = 4.0f * vector_magnitude *
                    (float32_t)APP_ADC_REFERENCE_MV /
                    (used_window_sum *
                     (float32_t)APP_ADC_FULL_SCALE_CODE);
  if (peak_to_peak_mv > (float32_t)APP_ADC_REFERENCE_MV)
    peak_to_peak_mv = (float32_t)APP_ADC_REFERENCE_MV;

  return (peak_to_peak_mv > 0.0f) ?
         (uint32_t)(peak_to_peak_mv + 0.5f) : 0U;
}

static float32_t FFT_EstimatePhaseRadians(const uint16_t *adc_buffer,
                                          uint32_t size,
                                          float32_t dc_offset,
                                          float32_t fractional_bin,
                                          uint32_t requested_phase_size)
{
  uint32_t phase_size;
  uint32_t start;
  float32_t step_angle = 2.0f * PI * fractional_bin /
                         (float32_t)BUFFER_SIZE;
  float32_t step_cosine = arm_cos_f32(step_angle);
  float32_t step_sine = arm_sin_f32(step_angle);
  float32_t cosine_value = 1.0f;
  float32_t sine_value = 0.0f;
  float32_t in_phase = 0.0f;
  float32_t quadrature = 0.0f;
  uint32_t i;

  phase_size = (size < requested_phase_size) ? size : requested_phase_size;
  start = size - phase_size;

  /* Low frequencies use the newest quarter-frame for response speed. At high
   * frequencies the full frame contains many cycles and lowers phase noise. */
  for (i = 0U; i < phase_size; i++)
  {
    uint32_t window_index = (i * BUFFER_SIZE) / phase_size;
    float32_t weighted_sample =
        ((float32_t)adc_buffer[start + i] - dc_offset) *
        hann_window[window_index];
    float32_t next_cosine;
    float32_t next_sine;

    in_phase += weighted_sample * cosine_value;
    quadrature -= weighted_sample * sine_value;

    next_cosine = cosine_value * step_cosine - sine_value * step_sine;
    next_sine = sine_value * step_cosine + cosine_value * step_sine;
    cosine_value = next_cosine;
    sine_value = next_sine;
  }

  if (fabs((double)in_phase) + fabs((double)quadrature) <= 1.0e-6)
    return 0.0f;

  /* y=A*sin(w*n+phase): cos projection is sin(phase). */
  return (float32_t)atan2((double)in_phase, (double)-quadrature);
}

uint8_t FFT_EstimatePhaseAtFrequency(const uint16_t *adc_buffer,
                                     uint32_t size,
                                     float32_t sample_frequency_hz,
                                     float32_t reference_frequency_hz,
                                     uint32_t phase_window_size,
                                     float32_t *phase_radians)
{
  uint32_t copy_size;
  uint32_t sum = 0U;
  float32_t dc_offset;
  float32_t fractional_bin;
  uint32_t i;

  if (phase_radians != NULL)
    *phase_radians = 0.0f;
  if (adc_buffer == NULL || phase_radians == NULL || size == 0U ||
      sample_frequency_hz <= 0.0f || reference_frequency_hz <= 0.0f ||
      reference_frequency_hz >= sample_frequency_hz * 0.5f ||
      phase_window_size == 0U || phase_window_size > BUFFER_SIZE)
  {
    return 0U;
  }

  if (!fft_initialized)
    FFT_Init();

  copy_size = (size < BUFFER_SIZE) ? size : BUFFER_SIZE;
  for (i = 0U; i < copy_size; i++)
    sum += adc_buffer[i];
  dc_offset = (float32_t)sum / (float32_t)copy_size;
  fractional_bin = reference_frequency_hz * (float32_t)BUFFER_SIZE /
                   sample_frequency_hz;
  *phase_radians = FFT_EstimatePhaseRadians(adc_buffer,
                                             copy_size,
                                             dc_offset,
                                             fractional_bin,
                                             phase_window_size);
  return 1U;
}

uint32_t FFT_GetPhaseWindowSize(uint32_t size,
                                float32_t sample_frequency_hz,
                                float32_t reference_frequency_hz)
{
  float32_t fractional_bin;
  uint32_t requested_phase_size;

  if (size == 0U || sample_frequency_hz <= 0.0f ||
      reference_frequency_hz <= 0.0f)
  {
    return 0U;
  }

  fractional_bin = reference_frequency_hz * (float32_t)BUFFER_SIZE /
                   sample_frequency_hz;
  if (fractional_bin >= FFT_PHASE_WINDOW_HIGH_BIN)
    requested_phase_size = BUFFER_SIZE;
  else if (fractional_bin >= FFT_PHASE_WINDOW_MID_BIN)
    requested_phase_size = FFT_PHASE_WINDOW_MID_SIZE;
  else
    requested_phase_size = FFT_PHASE_WINDOW_MIN_SIZE;

  return (size < requested_phase_size) ? size : requested_phase_size;
}

void FFT_Init(void)
{
  float32_t cosine_value = 1.0f;
  float32_t sine_value = 0.0f;
  const float32_t step_cosine = 0.99998116f;
  const float32_t step_sine = 0.00614188f;
  uint32_t i;

  if (fft_initialized)
    return;

  for (i = 0U; i < BUFFER_SIZE; i++)
  {
    float32_t next_cosine;
    float32_t next_sine;

    hann_window[i] = 0.5f - 0.5f * cosine_value;
    if (hann_window[i] < 0.0f)
      hann_window[i] = 0.0f;

    next_cosine = cosine_value * step_cosine - sine_value * step_sine;
    next_sine = sine_value * step_cosine + cosine_value * step_sine;
    cosine_value = next_cosine;
    sine_value = next_sine;
  }

  fft_initialized = 1U;
}

uint8_t FFT_DetectFrequency(const uint16_t *adc_buffer,
                            uint32_t size,
                            float32_t sample_frequency_hz,
                            PeakInfo_t *peak)
{
  PeakInfo_t result = {0};
  uint32_t copy_size;
  uint32_t sum = 0U;
  float32_t dc_offset;
  float32_t maximum_magnitude = 0.0f;
  uint32_t maximum_index = 0U;
  uint32_t i;

  if (peak != NULL)
    *peak = result;
  if (adc_buffer == NULL || peak == NULL || size == 0U ||
      sample_frequency_hz <= 0.0f)
  {
    return 0U;
  }

  if (!fft_initialized)
    FFT_Init();

  copy_size = (size < BUFFER_SIZE) ? size : BUFFER_SIZE;
  for (i = 0U; i < copy_size; i++)
    sum += adc_buffer[i];
  dc_offset = (float32_t)sum / (float32_t)copy_size;

  for (i = 0U; i < copy_size; i++)
  {
    fft_buffer[2U * i] =
        ((float32_t)adc_buffer[i] - dc_offset) * hann_window[i];
    fft_buffer[2U * i + 1U] = 0.0f;
  }
  for (i = copy_size; i < BUFFER_SIZE; i++)
  {
    fft_buffer[2U * i] = 0.0f;
    fft_buffer[2U * i + 1U] = 0.0f;
  }

  arm_cfft_f32(&fft_instance, fft_buffer, 0U, 1U);
  arm_cmplx_mag_f32(fft_buffer,
                    magnitude_buffer,
                    FFT_POSITIVE_BIN_COUNT);

  for (i = 1U; i < BUFFER_SIZE / 2U; i++)
  {
    if (magnitude_buffer[i] > maximum_magnitude)
    {
      maximum_magnitude = magnitude_buffer[i];
      maximum_index = i;
    }
  }

  if (maximum_index == 0U)
    return 1U;

  if (maximum_index < (BUFFER_SIZE / 2U - 1U))
  {
    float32_t left = magnitude_buffer[maximum_index - 1U];
    float32_t center = magnitude_buffer[maximum_index];
    float32_t right = magnitude_buffer[maximum_index + 1U];
    float32_t denominator = left + 2.0f * center + right;

    if (denominator > 1.0e-12f)
    {
      /* Hann-specific estimator is less biased than a generic parabola. */
      result.bin_offset = 2.0f * (right - left) / denominator;
      if (result.bin_offset > 0.5f)
        result.bin_offset = 0.5f;
      else if (result.bin_offset < -0.5f)
        result.bin_offset = -0.5f;
    }
  }

  result.index = maximum_index;
  result.frequency = ((float32_t)maximum_index + result.bin_offset) *
                     sample_frequency_hz / (float32_t)BUFFER_SIZE;
  result.peak_to_peak_mv =
      FFT_EstimatePeakToPeakMv(adc_buffer,
                               copy_size,
                               dc_offset,
                               (float32_t)maximum_index + result.bin_offset);
  /* 相位不在这里算：反馈路必须在"参考频率 × 倍频数"处投影，
   * 用本路自己的谱峰频率会测错，统一由调用方 FFT_EstimatePhaseAtFrequency 填。 */
  result.valid = 1U;
  *peak = result;
  return 1U;
}
