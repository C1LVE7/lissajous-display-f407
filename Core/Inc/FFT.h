/**
  ******************************************************************************
  * @file    FFT.h
  * @brief   1024-point ADC fundamental-frequency detector.
  ******************************************************************************
  */

#ifndef __FFT_H
#define __FFT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "arm_math.h"

#define BUFFER_SIZE 1024U

typedef struct
{
  uint32_t index;             /**< Strongest positive-frequency FFT bin. */
  float32_t bin_offset;       /**< Hann-window interpolation, -0.5 to +0.5 bin. */
  float32_t frequency;        /**< Interpolated fundamental frequency in Hz. */
  float32_t phase_radians;    /**< 由调用方用 FFT_EstimatePhaseAtFrequency 填。 */
  uint32_t peak_to_peak_mv;   /**< ADC-pin Vpp, clamped to 0..3300 mV. */
  uint8_t valid;              /**< One means a non-DC peak was found. */
} PeakInfo_t;

/** Precompute the Hann window. Repeated calls have no effect. */
void FFT_Init(void);

/**
  * Detect the strongest non-DC component in one ADC frame.
  * Frequency uses a Hann-window three-bin estimator. Amplitude is fitted at
  * the interpolated frequency, avoiding time-domain peak-sampling error.
  * phase_radians is left at zero: the caller projects both channels at the
  * same reference frequency and writes the result itself.
  */
uint8_t FFT_DetectFrequency(const uint16_t *adc_buffer,
                            uint32_t size,
                            float32_t sample_frequency_hz,
                            PeakInfo_t *peak);

/** Estimate scalar phase at an explicitly shared reference frequency. */
uint8_t FFT_EstimatePhaseAtFrequency(const uint16_t *adc_buffer,
                                     uint32_t size,
                                     float32_t sample_frequency_hz,
                                     float32_t reference_frequency_hz,
                                     uint32_t phase_window_size,
                                     float32_t *phase_radians);

/** Select a 256/512/1024-point phase window for a reference frequency. */
uint32_t FFT_GetPhaseWindowSize(uint32_t size,
                                float32_t sample_frequency_hz,
                                float32_t reference_frequency_hz);

#ifdef __cplusplus
}
#endif

#endif /* __FFT_H */
