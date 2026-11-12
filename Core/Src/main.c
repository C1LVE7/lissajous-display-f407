/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "dma.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "signal_app.h"
#include "tft_display.h"
#include "vision_slave.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

typedef enum
{
  APP_MODE_NORMAL = 0U,
  APP_MODE_VISION = 1U
} AppMode;

/* 一个按键的消抖状态，每个键各存一份 */
typedef struct
{
  GPIO_PinState sampled_state;
  GPIO_PinState stable_state;
  uint32_t state_change_tick;
} ButtonState_t;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define BUTTON_DEBOUNCE_MS         20U
#define OUTPUT_SELECT_COUNT         4U
#define OUTPUT_SELECT_ALL_PINS (SELECT_OUTPUT_PB4_Pin | \
                                SELECT_OUTPUT_PB5_Pin | \
                                SELECT_OUTPUT_PB6_Pin | \
                                SELECT_OUTPUT_PB7_Pin)

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
static AppMode app_mode = APP_MODE_NORMAL;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static uint8_t DDS_DoubleButtonProcess(void);
static uint8_t DDS_PhaseButtonProcess(void);
static uint8_t QuestionSelectButton_Process(void);
static uint8_t AppMode_EnterVision(void);
static void OutputSelector_Process(void);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/*
 * 通用按键消抖：电平稳定超过 BUTTON_DEBOUNCE_MS 才算一次按下。
 * 返回 1 表示这一圈检测到一次有效按下，返回 0 表示没有。
 * 上电第一次调用只对齐电平，不判定；上电时按住不放的键必须先松开，才认第一次按下。
 */
static uint8_t Button_WasPressed(ButtonState_t *button,
                                 GPIO_TypeDef *port,
                                 uint16_t pin)
{
  GPIO_PinState current_state = HAL_GPIO_ReadPin(port, pin);
  uint32_t now = HAL_GetTick();

  /* state_change_tick 为 0 表示还没对齐过，这一次只记录电平，不做判定 */
  if (button->state_change_tick == 0U)
  {
    button->sampled_state = current_state;
    button->stable_state = current_state;
    button->state_change_tick = now;
    return 0U;
  }

  if (current_state != button->sampled_state)
  {
    button->sampled_state = current_state;
    button->state_change_tick = now;
  }

  if (button->sampled_state != button->stable_state &&
      (uint32_t)(now - button->state_change_tick) >= BUTTON_DEBOUNCE_MS)
  {
    button->stable_state = button->sampled_state;
    if (button->stable_state == GPIO_PIN_SET)
      return 1U;
  }

  return 0U;
}

static uint8_t DDS_DoubleButtonProcess(void)
{
  static ButtonState_t button;

  if (!Button_WasPressed(&button, DDS_DOUBLE_BUTTON_GPIO_Port,
                         DDS_DOUBLE_BUTTON_Pin))
  {
    return 1U;
  }

  if (SignalApp_ToggleOutputMode() == 0U)
    return 0U;
  TFT_Display_ShowQuestion(3U);
  return 1U;
}

static uint8_t DDS_PhaseButtonProcess(void)
{
  static ButtonState_t button;

  if (!Button_WasPressed(&button, DDS_PHASE_BUTTON_GPIO_Port,
                         DDS_PHASE_BUTTON_Pin))
  {
    return 1U;
  }

  if (SignalApp_TogglePhaseTarget() == 0U)
    return 0U;
  TFT_Display_ShowQuestion(2U);
  return 1U;
}

static uint8_t QuestionSelectButton_Process(void)
{
  static ButtonState_t button;

  return Button_WasPressed(&button, QUESTION_SELECT_BUTTON_GPIO_Port,
                           QUESTION_SELECT_BUTTON_Pin);
}

static uint8_t AppMode_EnterVision(void)
{
  if (!SignalApp_Suspend())
    return 0U;

  MX_USART3_UART_Init();
  if (!VisionSlave_Init(&huart3))
  {
    (void)HAL_UART_DeInit(&huart3);
    /* 回滚采集只为把现场清干净，之后仍然返 0 交给上层停机 */
    (void)SignalApp_Resume(&huart1);
    return 0U;
  }

  TFT_Display_ShowVision(0U);
  app_mode = APP_MODE_VISION;
  return 1U;
}

static void OutputSelector_SetChannel(uint8_t channel)
{
  static const uint16_t output_pins[OUTPUT_SELECT_COUNT] =
  {
    SELECT_OUTPUT_PB4_Pin,
    SELECT_OUTPUT_PB5_Pin,
    SELECT_OUTPUT_PB6_Pin,
    SELECT_OUTPUT_PB7_Pin
  };
  uint16_t selected_pin = output_pins[channel];
  uint16_t other_pins = (uint16_t)(OUTPUT_SELECT_ALL_PINS &
                                   (uint16_t)~selected_pin);

  SELECT_OUTPUT_PB4_GPIO_Port->BSRR =
      ((uint32_t)selected_pin << 16U) | (uint32_t)other_pins;
}

static void OutputSelector_Process(void)
{
  static ButtonState_t button;
  static uint8_t selected_channel = 0U;
  static const uint8_t vertical_divisions[OUTPUT_SELECT_COUNT] =
  {
    8U, 6U, 4U, 2U
  };

  if (!Button_WasPressed(&button, OUTPUT_SELECT_BUTTON_GPIO_Port,
                         OUTPUT_SELECT_BUTTON_Pin))
  {
    return;
  }

  selected_channel =
      (uint8_t)((selected_channel + 1U) % OUTPUT_SELECT_COUNT);
  OutputSelector_SetChannel(selected_channel);
  /* 屏幕显示跟着真正被拉低的那一位走。 */
  if (app_mode == APP_MODE_NORMAL)
    TFT_Display_ShowFourth(vertical_divisions[selected_channel]);
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_TIM3_Init();
  MX_ADC1_Init();
  MX_ADC2_Init();
  MX_USART1_UART_Init();
/* USER CODE BEGIN 2 */
  TFT_Display_Init();
  TFT_Display_ShowQuestion(1U);
  if (!SignalApp_Init(&hadc1, &hadc2, &htim3, &huart1))
    Error_Handler();
/* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    if (app_mode == APP_MODE_NORMAL && QuestionSelectButton_Process())
    {
      if (!AppMode_EnterVision())
        Error_Handler();
      continue;
    }

    OutputSelector_Process();
    if (app_mode == APP_MODE_NORMAL)
    {
      if (!DDS_DoubleButtonProcess() ||
          !DDS_PhaseButtonProcess() ||
          !SignalApp_Process())
      {
        Error_Handler();
      }
    }
    else if (!VisionSlave_Process())
    {
      Error_Handler();
    }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 25;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
