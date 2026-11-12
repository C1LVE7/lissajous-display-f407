/**
  * @file    dds_pll.c
  * @brief   Scalar FFT phase detector steering DDS frequency continuously.
  */

#include "dds_pll.h"

#include "app_config.h"

#define DDS_PLL_FREQUENCY_FILTER_ALPHA 0.25f

static float32_t DdsPll_WrapPhase(float32_t phase)
{
  while (phase > PI)
    phase -= 2.0f * PI;
  while (phase < -PI)
    phase += 2.0f * PI;
  return phase;
}

static int32_t DdsPll_AbsI32(int32_t value)
{
  return (value < 0) ? -value : value;
}

static int32_t DdsPll_PpmLimit(uint32_t frequency_millihz,
                               uint32_t ppm,
                               int32_t minimum_millihz)
{
  uint64_t scaled_limit =
      ((uint64_t)frequency_millihz * ppm + 999999ULL) / 1000000ULL;

  if (scaled_limit < (uint64_t)minimum_millihz)
    return minimum_millihz;
  if (scaled_limit > 0x7FFFFFFFULL)
    return 0x7FFFFFFFL;
  return (int32_t)scaled_limit;
}

void DdsPll_Reset(DdsPllState *state,
                  uint32_t nominal_frequency_millihz)
{
  if (state == NULL)
    return;

  /* 目标相位是用户设定，不属于环路运行状态，这里不动它。 */
  state->tracked_frequency_millihz =
      (float32_t)nominal_frequency_millihz;
  state->phase_error_mdeg = 0;
  state->frequency_error_millihz = 0;
  state->frequency_trim_millihz = 0;
  state->phase_frequency_correction_millihz = 0;
  state->commanded_frequency_millihz = nominal_frequency_millihz;
  state->measurement_valid = 0U;
  state->locked = 0U;
  state->good_count = 0U;
}

void DdsPll_Invalidate(DdsPllState *state)
{
  if (state == NULL)
    return;

  state->measurement_valid = 0U;
  state->locked = 0U;
  state->good_count = 0U;
}

uint8_t DdsPll_Update(DdsPllState *state,
                      AD9834_GPIO_HandleTypeDef *dds,
                      const PeakInfo_t *input_peak,
                      const PeakInfo_t *feedback_peak,
                      uint8_t frequency_multiplier,
                      uint32_t nominal_frequency_millihz,
                      int32_t target_phase_mdeg)
{
  float32_t measured_frequency_millihz;
  float32_t target_phase;
  float32_t phase_error;
  float32_t integral_step_f;
  float32_t phase_frequency_gain;
  float32_t phase_frequency_correction_f;
  uint32_t feedback_frequency_millihz;
  uint32_t tracked_frequency_millihz;
  uint32_t requested_frequency_millihz;
  uint64_t maximum_frequency_millihz;
  float32_t requested_frequency_millihz_f;
  int32_t capture_frequency_limit_millihz;
  int32_t capture_frequency_error_millihz;
  int32_t frequency_trim_limit_millihz;
  int32_t integral_step_millihz;
  int32_t lock_frequency_limit_millihz;

  if (state != NULL)
    state->target_phase_mdeg = target_phase_mdeg;

  if (state == NULL || dds == NULL || input_peak == NULL ||
      feedback_peak == NULL || frequency_multiplier == 0U ||
      nominal_frequency_millihz == 0U ||
      input_peak->valid == 0U || feedback_peak->valid == 0U)
  {
    DdsPll_Invalidate(state);
    return 1U;
  }

  measured_frequency_millihz =
      input_peak->frequency * (float32_t)frequency_multiplier * 1000.0f;
  if (state->measurement_valid == 0U)
  {
    state->tracked_frequency_millihz = measured_frequency_millihz;
  }
  else
  {
    state->tracked_frequency_millihz +=
        DDS_PLL_FREQUENCY_FILTER_ALPHA *
        (measured_frequency_millihz - state->tracked_frequency_millihz);
  }

  feedback_frequency_millihz =
      (uint32_t)(feedback_peak->frequency * 1000.0f + 0.5f);
  tracked_frequency_millihz =
      (uint32_t)(state->tracked_frequency_millihz + 0.5f);
  state->frequency_error_millihz = (int32_t)tracked_frequency_millihz -
                                   (int32_t)feedback_frequency_millihz;
  capture_frequency_limit_millihz =
      DdsPll_PpmLimit(tracked_frequency_millihz,
                      APP_PLL_CAPTURE_FREQUENCY_PPM,
                      APP_PLL_CAPTURE_FREQUENCY_FLOOR_MILLIHZ);
  capture_frequency_error_millihz =
      (int32_t)state->commanded_frequency_millihz -
      (int32_t)feedback_frequency_millihz;
  if (DdsPll_AbsI32(capture_frequency_error_millihz) >
      capture_frequency_limit_millihz)
  {
    DdsPll_Invalidate(state);
    return 1U;
  }

  target_phase = input_peak->phase_radians * (float32_t)frequency_multiplier;
  target_phase += (float32_t)target_phase_mdeg *
                  (PI / 180000.0f);
  phase_error = DdsPll_WrapPhase(target_phase -
                                 feedback_peak->phase_radians);
  state->phase_error_mdeg = (int32_t)(phase_error * 180000.0f /
                                      PI +
                                      ((phase_error >= 0.0f) ? 0.5f : -0.5f));
  state->measurement_valid = 1U;

  frequency_trim_limit_millihz =
      DdsPll_PpmLimit(nominal_frequency_millihz,
                      APP_PLL_MAX_FREQUENCY_TRIM_PPM,
                      0L);
  if (DdsPll_AbsI32(state->phase_error_mdeg) >
          APP_PLL_PHASE_DEADBAND_MDEG &&
      DdsPll_AbsI32(state->phase_error_mdeg) <=
          APP_PLL_INTEGRAL_PHASE_GATE_MDEG)
  {
    integral_step_f = (float32_t)state->phase_error_mdeg *
                      APP_PLL_PHASE_FREQUENCY_KI;
    integral_step_millihz =
        (int32_t)(integral_step_f +
                  ((integral_step_f >= 0.0f) ? 0.5f : -0.5f));
    state->frequency_trim_millihz += integral_step_millihz;
  }
  if (state->frequency_trim_millihz > frequency_trim_limit_millihz)
    state->frequency_trim_millihz = frequency_trim_limit_millihz;
  else if (state->frequency_trim_millihz < -frequency_trim_limit_millihz)
    state->frequency_trim_millihz = -frequency_trim_limit_millihz;

  phase_frequency_gain = APP_PLL_PHASE_FREQUENCY_KP;
  if (DdsPll_AbsI32(state->phase_error_mdeg) >=
      APP_PLL_FAST_PHASE_THRESHOLD_MDEG)
  {
    phase_frequency_gain = APP_PLL_FAST_PHASE_FREQUENCY_KP;
  }
  phase_frequency_correction_f =
      (float32_t)state->phase_error_mdeg *
      phase_frequency_gain;
  if (DdsPll_AbsI32(state->phase_error_mdeg) <=
      APP_PLL_PHASE_DEADBAND_MDEG)
  {
    phase_frequency_correction_f = 0.0f;
  }
  if (phase_frequency_correction_f >
      (float32_t)APP_PLL_MAX_PHASE_FREQUENCY_MILLIHZ)
  {
    phase_frequency_correction_f =
        (float32_t)APP_PLL_MAX_PHASE_FREQUENCY_MILLIHZ;
  }
  else if (phase_frequency_correction_f <
           -(float32_t)APP_PLL_MAX_PHASE_FREQUENCY_MILLIHZ)
  {
    phase_frequency_correction_f =
        -(float32_t)APP_PLL_MAX_PHASE_FREQUENCY_MILLIHZ;
  }
  state->phase_frequency_correction_millihz =
      (int32_t)(phase_frequency_correction_f +
                ((phase_frequency_correction_f >= 0.0f) ? 0.5f : -0.5f));

  maximum_frequency_millihz = (uint64_t)dds->mclk_hz * 500U;
  if (maximum_frequency_millihz > 0xFFFFFFFFULL)
    maximum_frequency_millihz = 0xFFFFFFFFULL;
  requested_frequency_millihz_f =
      (float32_t)nominal_frequency_millihz +
      (float32_t)state->frequency_trim_millihz +
      (float32_t)state->phase_frequency_correction_millihz;
  if (requested_frequency_millihz_f < 1.0f)
    requested_frequency_millihz = 1U;
  else if ((uint64_t)requested_frequency_millihz_f >
           maximum_frequency_millihz)
  {
    requested_frequency_millihz = (uint32_t)maximum_frequency_millihz;
  }
  else
  {
    requested_frequency_millihz =
        (uint32_t)(requested_frequency_millihz_f + 0.5f);
  }

  if (DdsPll_AbsI32((int32_t)requested_frequency_millihz -
                    (int32_t)state->commanded_frequency_millihz) >=
      APP_PLL_COMMAND_DEADBAND_MILLIHZ)
  {
    if (AD9834_GPIO_SetFrequencyMilliHz(dds,
                                        requested_frequency_millihz) != HAL_OK)
    {
      DdsPll_Invalidate(state);
      return 0U;
    }
    state->commanded_frequency_millihz = requested_frequency_millihz;
  }

  lock_frequency_limit_millihz =
      DdsPll_PpmLimit(tracked_frequency_millihz,
                      APP_PLL_LOCK_FREQUENCY_PPM,
                      APP_PLL_LOCK_FREQUENCY_FLOOR_MILLIHZ);
  if (DdsPll_AbsI32(state->phase_error_mdeg) <= APP_PLL_LOCK_PHASE_MDEG &&
      DdsPll_AbsI32(state->frequency_error_millihz) <=
          lock_frequency_limit_millihz)
  {
    if (state->good_count < APP_PLL_LOCK_COUNT)
      state->good_count++;
  }
  else
  {
    state->good_count = 0U;
  }
  state->locked = (state->good_count >= APP_PLL_LOCK_COUNT) ? 1U : 0U;

  return 1U;
}
