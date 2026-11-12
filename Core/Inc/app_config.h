/**
  ******************************************************************************
  * @file    app_config.h
  * @brief   F-problem acquisition and DDS parameters.
  ******************************************************************************
  */

#ifndef __APP_CONFIG_H
#define __APP_CONFIG_H

#define APP_SAMPLE_TIMER_CLOCK_HZ       84000000UL
#define APP_SAMPLE_RATE_MAX_HZ          1000000UL
#define APP_SAMPLE_RATE_MIN_HZ          1000UL
#define APP_SAMPLE_RATE_STEP            10UL
#define APP_AUTORANGE_MIN_PEAK_BIN      8U
#define APP_AUTORANGE_MAX_PEAK_BIN      400U
#define APP_AUTORANGE_RECHECK_MS        5000U
#define APP_ANALYSIS_INTERVAL_MS         5U
#define APP_UART_PRINT_INTERVAL_MS       200U

/* Software DDS PLL. Phase error steers DDS frequency continuously; PHASE0 is
 * kept fixed while the output is running to avoid waveform discontinuities. */
#define APP_PLL_DEFAULT_TARGET_MDEG      0L
#define APP_PLL_ALTERNATE_TARGET_MDEG 90000L
#define APP_PLL_PHASE_DEADBAND_MDEG        100L
#define APP_PLL_PHASE_FREQUENCY_KP         0.20f
#define APP_PLL_FAST_PHASE_THRESHOLD_MDEG 10000L
#define APP_PLL_FAST_PHASE_FREQUENCY_KP     0.30f
#define APP_PLL_PHASE_FREQUENCY_KI         0.02f
#define APP_PLL_INTEGRAL_PHASE_GATE_MDEG 20000L
#define APP_PLL_MAX_PHASE_FREQUENCY_MILLIHZ 75000L
#define APP_PLL_COMMAND_DEADBAND_MILLIHZ    500L
#define APP_PLL_MAX_FREQUENCY_TRIM_PPM  20000UL
#define APP_PLL_CAPTURE_FREQUENCY_FLOOR_MILLIHZ 50000L
#define APP_PLL_CAPTURE_FREQUENCY_PPM     5000UL
#define APP_PLL_LOCK_PHASE_MDEG         5000L
#define APP_PLL_LOCK_FREQUENCY_FLOOR_MILLIHZ 5000L
#define APP_PLL_LOCK_FREQUENCY_PPM        500UL
#define APP_PLL_LOCK_COUNT              5U
#define APP_DDS_INVALID_MUTE_COUNT       20U

/* 两路前端都是 1:1 直连，测到的引脚峰峰值就是实际电压。 */
#define APP_ADC_REFERENCE_MV           3300UL
#define APP_ADC_FULL_SCALE_CODE        4095UL

/* The source frequency specified by the problem changes in 100 Hz steps. */
#define APP_INPUT_FREQUENCY_MIN_HZ     1000UL
#define APP_INPUT_FREQUENCY_MAX_HZ   100000UL
#define APP_INPUT_FREQUENCY_STEP_HZ      100UL
#define APP_INPUT_MIN_VPP_ADC_CODES      124U
#if APP_SAMPLE_RATE_MIN_HZ == 0U
#error "The minimum sample rate must be greater than zero"
#endif
#if APP_SAMPLE_RATE_MAX_HZ < APP_SAMPLE_RATE_MIN_HZ
#error "The maximum sample rate must not be lower than the minimum"
#endif
#if APP_SAMPLE_RATE_MAX_HZ > APP_SAMPLE_TIMER_CLOCK_HZ
#error "The sample rate must not exceed the TIM3 input clock"
#endif
#if APP_SAMPLE_RATE_STEP < 2U
#error "The autorange ratio must be at least two"
#endif
#if APP_AUTORANGE_MIN_PEAK_BIN >= APP_AUTORANGE_MAX_PEAK_BIN
#error "The autorange FFT-bin thresholds are invalid"
#endif
#if APP_ADC_REFERENCE_MV == 0U || APP_ADC_FULL_SCALE_CODE == 0U
#error "The ADC voltage conversion constants must be greater than zero"
#endif
#if APP_INPUT_FREQUENCY_MIN_HZ == 0U || \
    APP_INPUT_FREQUENCY_MAX_HZ < APP_INPUT_FREQUENCY_MIN_HZ
#error "The supported input frequency range is invalid"
#endif
#if (APP_INPUT_FREQUENCY_MAX_HZ * 2U) >= (APP_SAMPLE_RATE_MAX_HZ / 2U)
#error "The double-frequency DDS feedback must remain below Nyquist"
#endif

#endif /* __APP_CONFIG_H */
