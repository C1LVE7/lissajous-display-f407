/**
  * @file    dds_pll.h
  * @brief   Continuous-phase frequency PI loop for the AD9834.
  */

#ifndef __DDS_PLL_H
#define __DDS_PLL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "FFT.h"
#include "ad9834_gpio_f407.h"

typedef struct
{
  float32_t tracked_frequency_millihz;
  int32_t phase_error_mdeg;
  int32_t target_phase_mdeg;
  int32_t frequency_error_millihz;
  int32_t frequency_trim_millihz;
  int32_t phase_frequency_correction_millihz;
  uint32_t commanded_frequency_millihz;
  uint8_t measurement_valid;
  uint8_t locked;
  uint8_t good_count;
} DdsPllState;

void DdsPll_Reset(DdsPllState *state,
                  uint32_t nominal_frequency_millihz);

/** Clear lock qualification while preserving the active DDS command/phase. */
void DdsPll_Invalidate(DdsPllState *state);

uint8_t DdsPll_Update(DdsPllState *state,
                      AD9834_GPIO_HandleTypeDef *dds,
                      const PeakInfo_t *input_peak,
                      const PeakInfo_t *feedback_peak,
                      uint8_t frequency_multiplier,
                      uint32_t nominal_frequency_millihz,
                      int32_t target_phase_mdeg);

#ifdef __cplusplus
}
#endif

#endif /* __DDS_PLL_H */
