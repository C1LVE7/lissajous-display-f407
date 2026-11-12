#ifndef TFT_DISPLAY_H
#define TFT_DISPLAY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void TFT_Display_Init(void);
void TFT_Display_ShowQuestion(uint8_t question_number);
void TFT_Display_ShowFourth(uint8_t vertical_divisions);
void TFT_Display_ShowVision(uint32_t frequency_millihz);
void TFT_Display_UpdateVisionFrequency(uint32_t frequency_millihz);

#ifdef __cplusplus
}
#endif

#endif /* TFT_DISPLAY_H */
