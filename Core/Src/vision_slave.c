/**
  ******************************************************************************
  * @file    vision_slave.c
  * @brief   Camera-only requirement-5 command executor.
  ******************************************************************************
  */

#include "vision_slave.h"

#include "ad9834_gpio_f407.h"
#include "tft_display.h"

#define VISION_PROTOCOL_VERSION          "F5_FFT_OU_SLAVE_1.4_NO_TIMEOUT"
#define VISION_LINE_SIZE                 128U
#define VISION_BUTTON_DEBOUNCE_MS         20U
#define VISION_NOTIFICATION_MS          3000U
#define VISION_PD12_PD13_MIN_MILLIHZ   1000000UL
#define VISION_PD12_PD13_MAX_MILLIHZ 100000000UL
#define VISION_PD3_MIN_MILLIHZ          2000000UL
#define VISION_PD3_MAX_MILLIHZ        200000000UL
#define VISION_RAMP_LIMIT_WPS            8192L
#define VISION_BUTTON_LINE                 0U
#define VISION_BUTTON_CIRCLE               1U
#define VISION_BUTTON_INFINITY             2U
#define VISION_BUTTON_NONE                0xFFU

typedef struct
{
  GPIO_TypeDef *port;
  uint16_t pin;
  GPIO_PinState active_state;
  GPIO_PinState sampled_state;
  GPIO_PinState stable_state;
  uint32_t change_tick;
  const char *start_message;
} VisionButton;

static UART_HandleTypeDef *vision_uart = NULL;
static AD9834_GPIO_HandleTypeDef vision_dds;
static uint8_t vision_dds_ready = 0U;
static char vision_line[VISION_LINE_SIZE];
static uint16_t vision_line_length = 0U;
static uint16_t vision_phase_word = 0U;
static uint32_t vision_frequency_millihz = 0U;
static int32_t vision_ramp_wps = 0L;
static int64_t vision_ramp_accumulator = 0LL;
static uint32_t vision_ramp_tick = 0U;
static uint32_t vision_notification_tick = 0U;
static uint8_t vision_run_active = 0U;
static uint8_t vision_selected_button = VISION_BUTTON_NONE;
static uint8_t vision_notification_active = 0U;

static VisionButton vision_buttons[] =
{
  {VISION_LINE_BUTTON_GPIO_Port, VISION_LINE_BUTTON_Pin,
   GPIO_PIN_SET, GPIO_PIN_RESET, GPIO_PIN_RESET, 0U, "$START,LINE\r\n"},
  {VISION_CIRCLE_BUTTON_GPIO_Port, VISION_CIRCLE_BUTTON_Pin,
   GPIO_PIN_SET, GPIO_PIN_RESET, GPIO_PIN_RESET, 0U, "$START,CIRCLE\r\n"},
  {VISION_INFINITY_BUTTON_GPIO_Port, VISION_INFINITY_BUTTON_Pin,
   GPIO_PIN_SET, GPIO_PIN_RESET, GPIO_PIN_RESET, 0U, "$START,INFINITY\r\n"}
};

static void VisionSlave_SendText(const char *text)
{
  uint16_t length = 0U;

  if (vision_uart == NULL || text == NULL)
    return;
  while (text[length] != '\0' && length < 127U)
    length++;
  if (length != 0U)
    (void)HAL_UART_Transmit(vision_uart, (uint8_t *)text, length, 50U);
}

static void VisionSlave_SetNotification(uint8_t enabled)
{
  GPIO_PinState state = (enabled != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET;

  HAL_GPIO_WritePin(VISION_BUZZER_GPIO_Port, VISION_BUZZER_Pin, state);
  HAL_GPIO_WritePin(VISION_DONE_GPIO_Port, VISION_DONE_Pin, state);
  vision_notification_active = (enabled != 0U) ? 1U : 0U;
  if (enabled != 0U)
    vision_notification_tick = HAL_GetTick();
}

static void VisionSlave_FinishRun(void)
{
  if (vision_run_active == 0U)
    return;

  vision_run_active = 0U;
  vision_selected_button = VISION_BUTTON_NONE;
  vision_ramp_wps = 0L;
  vision_ramp_accumulator = 0LL;
  VisionSlave_SetNotification(1U);
}

static void VisionSlave_SendInt32(int32_t value)
{
  char digits[11];
  uint32_t magnitude;
  uint8_t count = 0U;

  if (value < 0L)
  {
    VisionSlave_SendText("-");
    magnitude = (uint32_t)(-(value + 1L)) + 1U;
  }
  else
  {
    magnitude = (uint32_t)value;
  }

  do
  {
    digits[count++] = (char)('0' + magnitude % 10U);
    magnitude /= 10U;
  } while (magnitude != 0U && count < sizeof(digits));

  while (count != 0U)
  {
    char digit[2];
    digit[0] = digits[--count];
    digit[1] = '\0';
    VisionSlave_SendText(digit);
  }
}

static uint16_t VisionSlave_PhaseWord(int32_t phase_millideg)
{
  int32_t wrapped = phase_millideg % 360000L;
  if (wrapped < 0L)
    wrapped += 360000L;
  return (uint16_t)((((uint32_t)wrapped * 4096UL) + 180000UL) /
                    360000UL) & 0x0FFFU;
}

static uint8_t VisionSlave_FrequencyAllowed(uint32_t frequency_millihz)
{
  if (vision_selected_button == VISION_BUTTON_INFINITY)
  {
    return (uint8_t)(frequency_millihz >= VISION_PD3_MIN_MILLIHZ &&
                     frequency_millihz <= VISION_PD3_MAX_MILLIHZ);
  }
  if (vision_selected_button == VISION_BUTTON_LINE ||
      vision_selected_button == VISION_BUTTON_CIRCLE)
  {
    return (uint8_t)(frequency_millihz >= VISION_PD12_PD13_MIN_MILLIHZ &&
                     frequency_millihz <= VISION_PD12_PD13_MAX_MILLIHZ);
  }
  return 0U;
}

static HAL_StatusTypeDef VisionSlave_ApplyOutput(uint32_t frequency_millihz,
                                                  int32_t phase_millideg,
                                                  int32_t ramp_wps)
{
  HAL_StatusTypeDef status;

  if (!vision_dds_ready ||
      !VisionSlave_FrequencyAllowed(frequency_millihz) ||
      ramp_wps < -VISION_RAMP_LIMIT_WPS ||
      ramp_wps > VISION_RAMP_LIMIT_WPS)
  {
    return HAL_ERROR;
  }

  vision_phase_word = VisionSlave_PhaseWord(phase_millideg);
  status = AD9834_GPIO_SetSoftwareReset(&vision_dds, 1U);
  if (status == HAL_OK)
    status = AD9834_GPIO_SetWaveform(&vision_dds, AD9834_GPIO_WAVE_SINE);
  if (status == HAL_OK)
    status = AD9834_GPIO_SetFrequencyMilliHz(&vision_dds,
                                             frequency_millihz);
  if (status == HAL_OK)
    status = AD9834_GPIO_WritePhaseWord(&vision_dds,
                                        AD9834_GPIO_PHASE_REG0,
                                        vision_phase_word);
  if (status == HAL_OK)
    status = AD9834_GPIO_SelectPhaseRegister(&vision_dds,
                                             AD9834_GPIO_PHASE_REG0);
  if (status == HAL_OK)
    status = AD9834_GPIO_SetSoftwareReset(&vision_dds, 0U);
  if (status != HAL_OK)
  {
    (void)AD9834_GPIO_SetSoftwareReset(&vision_dds, 1U);
    return status;
  }

  vision_ramp_wps = ramp_wps;
  vision_ramp_accumulator = 0LL;
  vision_ramp_tick = HAL_GetTick();
  if (vision_frequency_millihz != frequency_millihz)
  {
    vision_frequency_millihz = frequency_millihz;
    TFT_Display_UpdateVisionFrequency(vision_frequency_millihz);
  }
  return HAL_OK;
}

static HAL_StatusTypeDef VisionSlave_ApplyFrequency(
    uint32_t frequency_millihz)
{
  HAL_StatusTypeDef status;

  if (!vision_dds_ready ||
      !VisionSlave_FrequencyAllowed(frequency_millihz))
  {
    return HAL_ERROR;
  }
  status = AD9834_GPIO_SetFrequencyMilliHz(&vision_dds,
                                           frequency_millihz);
  if (status == HAL_OK && vision_frequency_millihz != frequency_millihz)
  {
    vision_frequency_millihz = frequency_millihz;
    TFT_Display_UpdateVisionFrequency(vision_frequency_millihz);
  }
  return status;
}

static HAL_StatusTypeDef VisionSlave_ApplyPhase(int32_t phase_millideg)
{
  if (!vision_dds_ready)
    return HAL_ERROR;
  vision_phase_word = VisionSlave_PhaseWord(phase_millideg);
  vision_ramp_accumulator = 0LL;
  vision_ramp_tick = HAL_GetTick();
  return AD9834_GPIO_WritePhaseWord(&vision_dds,
                                    AD9834_GPIO_PHASE_REG0,
                                    vision_phase_word);
}

static void VisionSlave_StopOutput(void)
{
  vision_run_active = 0U;
  vision_ramp_wps = 0L;
  vision_ramp_accumulator = 0LL;
  if (vision_dds_ready != 0U)
    (void)AD9834_GPIO_SetSoftwareReset(&vision_dds, 1U);
  if (vision_frequency_millihz != 0U)
  {
    vision_frequency_millihz = 0U;
    TFT_Display_UpdateVisionFrequency(0U);
  }
}

static char *VisionSlave_NextField(char **cursor)
{
  char *field;
  char *comma;

  if (cursor == NULL || *cursor == NULL || **cursor == '\0')
    return NULL;
  field = *cursor;
  comma = field;
  while (*comma != '\0' && *comma != ',')
    comma++;
  if (*comma == '\0')
    comma = NULL;
  if (comma != NULL)
  {
    *comma = '\0';
    *cursor = comma + 1;
  }
  else
  {
    *cursor = NULL;
  }
  return field;
}

static uint8_t VisionSlave_ParseLong(char *field, int32_t *value)
{
  uint32_t magnitude = 0U;
  uint8_t negative = 0U;
  uint8_t digit_count = 0U;

  if (field == NULL || value == NULL)
    return 0U;
  if (*field == '-')
  {
    negative = 1U;
    field++;
  }
  while (*field != '\0')
  {
    uint32_t digit;
    if (*field < '0' || *field > '9')
      return 0U;
    digit = (uint32_t)(*field - '0');
    if (magnitude > 214748364U ||
        (magnitude == 214748364U && digit > 7U))
    {
      return 0U;
    }
    magnitude = magnitude * 10U + digit;
    digit_count++;
    field++;
  }
  if (digit_count == 0U)
    return 0U;
  *value = negative ? -(int32_t)magnitude : (int32_t)magnitude;
  return 1U;
}

static uint8_t VisionSlave_Equals(const char *left, const char *right)
{
  if (left == NULL || right == NULL)
    return 0U;
  while (*left != '\0' && *right != '\0' && *left == *right)
  {
    left++;
    right++;
  }
  return (uint8_t)(*left == '\0' && *right == '\0');
}

static void VisionSlave_Applied(int32_t seq, int32_t probe_id)
{
  VisionSlave_SendText("$APPLIED,");
  VisionSlave_SendInt32(seq);
  VisionSlave_SendText(",");
  VisionSlave_SendInt32(probe_id);
  VisionSlave_SendText("\r\n");
}

static void VisionSlave_Error(int32_t seq, int32_t probe_id,
                              const char *reason)
{
  VisionSlave_SendText("$ERROR,");
  VisionSlave_SendInt32(seq);
  VisionSlave_SendText(",");
  VisionSlave_SendInt32(probe_id);
  VisionSlave_SendText(",");
  VisionSlave_SendText(reason);
  VisionSlave_SendText("\r\n");
}

static void VisionSlave_HandleSet(char *cursor)
{
  int32_t seq;
  int32_t probe_id;
  int32_t frequency_millihz;
  int32_t phase_millideg;
  int32_t ramp_wps;

  if (!VisionSlave_ParseLong(VisionSlave_NextField(&cursor), &seq) ||
      !VisionSlave_ParseLong(VisionSlave_NextField(&cursor), &probe_id) ||
      !VisionSlave_ParseLong(VisionSlave_NextField(&cursor),
                             &frequency_millihz) ||
      !VisionSlave_ParseLong(VisionSlave_NextField(&cursor),
                             &phase_millideg) ||
      !VisionSlave_ParseLong(VisionSlave_NextField(&cursor), &ramp_wps))
  {
    VisionSlave_Error(0L, 0L, "BAD_SET");
    return;
  }
  if (vision_run_active == 0U)
  {
    VisionSlave_Error(seq, probe_id, "NOT_RUNNING");
    return;
  }
  if (frequency_millihz <= 0L ||
      VisionSlave_ApplyOutput((uint32_t)frequency_millihz,
                              phase_millideg, ramp_wps) != HAL_OK)
  {
    VisionSlave_Error(seq, probe_id, "DDS_SET");
    return;
  }
  VisionSlave_Applied(seq, probe_id);
}

static void VisionSlave_HandleFrequency(char *cursor)
{
  int32_t seq;
  int32_t probe_id;
  int32_t frequency_millihz;

  if (!VisionSlave_ParseLong(VisionSlave_NextField(&cursor), &seq) ||
      !VisionSlave_ParseLong(VisionSlave_NextField(&cursor), &probe_id) ||
      !VisionSlave_ParseLong(VisionSlave_NextField(&cursor),
                             &frequency_millihz))
  {
    VisionSlave_Error(0L, 0L, "BAD_FREQ");
    return;
  }
  if (vision_run_active == 0U)
  {
    VisionSlave_Error(seq, probe_id, "NOT_RUNNING");
    return;
  }
  if (frequency_millihz <= 0L ||
      VisionSlave_ApplyFrequency((uint32_t)frequency_millihz) != HAL_OK)
  {
    VisionSlave_Error(seq, probe_id, "DDS_FREQ");
    return;
  }
  VisionSlave_Applied(seq, probe_id);
}

static void VisionSlave_HandlePhase(char *cursor)
{
  int32_t seq;
  int32_t probe_id;
  int32_t phase_millideg;

  if (!VisionSlave_ParseLong(VisionSlave_NextField(&cursor), &seq) ||
      !VisionSlave_ParseLong(VisionSlave_NextField(&cursor), &probe_id) ||
      !VisionSlave_ParseLong(VisionSlave_NextField(&cursor), &phase_millideg))
  {
    VisionSlave_Error(0L, 0L, "BAD_PHASE");
    return;
  }
  if (vision_run_active == 0U)
  {
    VisionSlave_Error(seq, probe_id, "NOT_RUNNING");
    return;
  }
  if (VisionSlave_ApplyPhase(phase_millideg) != HAL_OK)
  {
    VisionSlave_Error(seq, probe_id, "DDS_PHASE");
    return;
  }
  VisionSlave_Applied(seq, probe_id);
}

static void VisionSlave_HandleRamp(char *cursor)
{
  int32_t seq;
  int32_t probe_id;
  int32_t ramp_wps;

  if (!VisionSlave_ParseLong(VisionSlave_NextField(&cursor), &seq) ||
      !VisionSlave_ParseLong(VisionSlave_NextField(&cursor), &probe_id) ||
      !VisionSlave_ParseLong(VisionSlave_NextField(&cursor), &ramp_wps) ||
      ramp_wps < -VISION_RAMP_LIMIT_WPS ||
      ramp_wps > VISION_RAMP_LIMIT_WPS)
  {
    VisionSlave_Error(0L, 0L, "BAD_RAMP");
    return;
  }
  if (vision_run_active == 0U)
  {
    VisionSlave_Error(seq, probe_id, "NOT_RUNNING");
    return;
  }
  vision_ramp_wps = ramp_wps;
  vision_ramp_accumulator = 0LL;
  vision_ramp_tick = HAL_GetTick();
  VisionSlave_Applied(seq, probe_id);
}

static void VisionSlave_HandleLine(char *line)
{
  char *cursor = line;
  char *command = VisionSlave_NextField(&cursor);

  if (command == NULL)
    return;
  if (command[0] == '$')
    command++;
  if (VisionSlave_Equals(command, "SET"))
    VisionSlave_HandleSet(cursor);
  else if (VisionSlave_Equals(command, "FREQ"))
    VisionSlave_HandleFrequency(cursor);
  else if (VisionSlave_Equals(command, "PHASE"))
    VisionSlave_HandlePhase(cursor);
  else if (VisionSlave_Equals(command, "RAMP"))
    VisionSlave_HandleRamp(cursor);
  else if (VisionSlave_Equals(command, "HELLO") ||
           VisionSlave_Equals(command, "K230_READY"))
  {
    VisionSlave_SendText("$MODE,VISION\r\n");
    VisionSlave_SendText("$STM32_READY," VISION_PROTOCOL_VERSION "\r\n");
  }
  else if (VisionSlave_Equals(command, "RUNNING"))
  {
    /* The PD button owns the run state until DONE, STOP, or another press. */
  }
  else if (VisionSlave_Equals(command, "LOCKED"))
  {
    /* DONE is the success latch; no DDS changes are accepted afterwards. */
  }
  else if (VisionSlave_Equals(command, "DONE"))
  {
    VisionSlave_FinishRun();
  }
  else if (VisionSlave_Equals(command, "FAIL"))
  {
    /* Freeze phase motion; another PD press can start a fresh run. */
    vision_ramp_wps = 0L;
    vision_ramp_accumulator = 0LL;
  }
  else if (VisionSlave_Equals(command, "STOP"))
  {
    VisionSlave_StopOutput();
    vision_selected_button = VISION_BUTTON_NONE;
    VisionSlave_SetNotification(0U);
  }
}

static void VisionSlave_ProcessByte(uint8_t byte)
{
  if (byte == '\r')
    return;
  if (byte == '\n')
  {
    if (vision_line_length > 0U)
    {
      vision_line[vision_line_length] = '\0';
      VisionSlave_HandleLine(vision_line);
      vision_line_length = 0U;
    }
    return;
  }
  if (vision_line_length + 1U < VISION_LINE_SIZE)
    vision_line[vision_line_length++] = (char)byte;
  else
    vision_line_length = 0U;
}

static void VisionSlave_ProcessRx(void)
{
  USART_TypeDef *instance;

  if (vision_uart == NULL || vision_uart->Instance == NULL)
    return;
  instance = vision_uart->Instance;
  while ((instance->SR & USART_SR_RXNE) != 0U)
  {
    uint8_t byte = (uint8_t)(instance->DR & 0xFFU);
    VisionSlave_ProcessByte(byte);
  }
}

static void VisionSlave_ProcessRamp(void)
{
  uint32_t now = HAL_GetTick();
  uint32_t elapsed = now - vision_ramp_tick;
  int64_t whole_words;

  if (elapsed == 0U || vision_ramp_wps == 0L || !vision_dds_ready)
    return;
  vision_ramp_tick = now;
  vision_ramp_accumulator += (int64_t)vision_ramp_wps * elapsed;
  whole_words = vision_ramp_accumulator / 1000LL;
  vision_ramp_accumulator -= whole_words * 1000LL;
  if (whole_words != 0LL)
  {
    int32_t next = (int32_t)vision_phase_word + (int32_t)whole_words;
    vision_phase_word = (uint16_t)next & 0x0FFFU;
    (void)AD9834_GPIO_WritePhaseWord(&vision_dds,
                                     AD9834_GPIO_PHASE_REG0,
                                     vision_phase_word);
  }
}

static void VisionSlave_ProcessNotification(void)
{
  if (vision_notification_active != 0U &&
      (uint32_t)(HAL_GetTick() - vision_notification_tick) >=
          VISION_NOTIFICATION_MS)
  {
    VisionSlave_SetNotification(0U);
  }
}

static void VisionSlave_ProcessButtons(void)
{
  uint32_t now = HAL_GetTick();
  uint32_t index;

  for (index = 0U;
       index < sizeof(vision_buttons) / sizeof(vision_buttons[0]);
       index++)
  {
    VisionButton *button = &vision_buttons[index];
    GPIO_PinState current = HAL_GPIO_ReadPin(button->port, button->pin);
    if (current != button->sampled_state)
    {
      button->sampled_state = current;
      button->change_tick = now;
    }
    if (button->stable_state != button->sampled_state &&
        (uint32_t)(now - button->change_tick) >= VISION_BUTTON_DEBOUNCE_MS)
    {
      button->stable_state = button->sampled_state;
      if (button->stable_state == button->active_state)
      {
        VisionSlave_SetNotification(0U);
        if (vision_run_active != 0U ||
            vision_selected_button != (uint8_t)index ||
            vision_frequency_millihz != 0U)
        {
          VisionSlave_StopOutput();
        }
        vision_selected_button = (uint8_t)index;
        vision_run_active = 1U;
        VisionSlave_SendText(button->start_message);
      }
    }
  }
}

uint8_t VisionSlave_Init(UART_HandleTypeDef *uart)
{
  uint32_t index;

  if (uart == NULL)
    return 0U;
  vision_uart = uart;
  vision_line_length = 0U;
  vision_phase_word = 0U;
  vision_frequency_millihz = 0U;
  vision_ramp_wps = 0L;
  vision_ramp_accumulator = 0LL;
  vision_notification_tick = 0U;
  vision_run_active = 0U;
  vision_selected_button = VISION_BUTTON_NONE;
  vision_notification_active = 0U;
  VisionSlave_SetNotification(0U);
  vision_dds_ready =
      (AD9834_GPIO_InitDefault(&vision_dds) == HAL_OK) ? 1U : 0U;
  if (!vision_dds_ready ||
      AD9834_GPIO_SetSoftwareReset(&vision_dds, 1U) != HAL_OK)
    return 0U;

  for (index = 0U;
       index < sizeof(vision_buttons) / sizeof(vision_buttons[0]);
       index++)
  {
    vision_buttons[index].sampled_state = HAL_GPIO_ReadPin(
        vision_buttons[index].port, vision_buttons[index].pin);
    vision_buttons[index].stable_state = vision_buttons[index].sampled_state;
    vision_buttons[index].change_tick = HAL_GetTick();
  }
  vision_ramp_tick = HAL_GetTick();
  VisionSlave_SendText("$MODE,VISION\r\n");
  VisionSlave_SendText("$STM32_READY," VISION_PROTOCOL_VERSION "\r\n");
  return 1U;
}

void VisionSlave_DeInit(void)
{
  UART_HandleTypeDef *uart = vision_uart;

  if (uart == NULL)
    return;
  VisionSlave_SendText("$MODE,NORMAL\r\n");
  VisionSlave_StopOutput();
  vision_selected_button = VISION_BUTTON_NONE;
  VisionSlave_SetNotification(0U);
  vision_dds_ready = 0U;
  vision_uart = NULL;
  vision_line_length = 0U;
}

uint8_t VisionSlave_Process(void)
{
  if (vision_uart == NULL || !vision_dds_ready)
    return 0U;
  VisionSlave_ProcessButtons();
  VisionSlave_ProcessRx();
  VisionSlave_ProcessRamp();
  VisionSlave_ProcessNotification();
  return 1U;
}
