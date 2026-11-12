#include "tft_display.h"

#include "stm32f4xx_hal.h"

#define TFT_WIDTH             480U
#define TFT_HEIGHT            320U
#define TFT_BACKGROUND_COLOR  0xFFFFU
#define TFT_TEXT_COLOR        0x001FU
#define TFT_VALUE_COLOR       0x0000U
#define TFT_VISION_VALUE_Y    205U
#define TFT_VISION_VALUE_H     40U
#define TFT_VISION_FONT_SCALE   5U

#define TFT_COMMAND_ADDRESS   0x60000000UL
#define TFT_DATA_ADDRESS      0x60020000UL
#define TFT_COMMAND           (*((volatile uint16_t *)TFT_COMMAND_ADDRESS))
#define TFT_DATA              (*((volatile uint16_t *)TFT_DATA_ADDRESS))

#define TFT_MULTIPLY_CHAR     0xD7U

/* 48x48 Chinese glyphs generated from Microsoft YaHei regular. */
static const uint64_t glyph_di[48U] =
{
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
  0x000000000000ULL, 0x000C00030000ULL, 0x001C00070000ULL,
  0x003C000F0000ULL, 0x0038000E0000ULL, 0x007FFF1FFFE0ULL,
  0x00FFFF9FFFF0ULL, 0x01FFFFBFFFF0ULL, 0x03FFFF3FFFE0ULL,
  0x07C70078E000ULL, 0x0F8780F0F000ULL, 0x0F03C0E07800ULL,
  0x060180403000ULL, 0x04FFFFFFFE00ULL, 0x00FFFFFFFF00ULL,
  0x00FFFFFFFF00ULL, 0x00FFFFFFFF00ULL, 0x000003C00700ULL,
  0x000003C00700ULL, 0x000003C00700ULL, 0x007FFFFFFF00ULL,
  0x00FFFFFFFF00ULL, 0x00FFFFFFFF00ULL, 0x00E003C00700ULL,
  0x01E003C00600ULL, 0x01C003C00000ULL, 0x01FFFFFFFFC0ULL,
  0x01FFFFFFFFC0ULL, 0x01FFFFFFFFC0ULL, 0x01FFFFFFFFC0ULL,
  0x00003FC001C0ULL, 0x00007BC003C0ULL, 0x0000F3C003C0ULL,
  0x0003E3C003C0ULL, 0x000FC3C003C0ULL, 0x003F83C3FF80ULL,
  0x00FE03C3FF80ULL, 0x07F803C3FF00ULL, 0x0FE003C1F800ULL,
  0x0F8003C00000ULL, 0x060003C00000ULL, 0x000003C00000ULL
};

static const uint64_t glyph_yi[48U] =
{
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
  0x000000000000ULL, 0x0FFFFFFFFFF0ULL, 0x0FFFFFFFFFF0ULL,
  0x0FFFFFFFFFF0ULL, 0x000000000000ULL, 0x000000000000ULL,
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL
};

static const uint64_t glyph_ti[48U] =
{
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
  0x01FFF0000000ULL, 0x03FFF9FFFFF0ULL, 0x03FFF9FFFFF0ULL,
  0x038039FFFFE0ULL, 0x03803801C000ULL, 0x03803801C000ULL,
  0x03FFF801C000ULL, 0x03FFF8038000ULL, 0x03FFF8FFFF80ULL,
  0x038038FFFF80ULL, 0x038038FFFF80ULL, 0x038038E00180ULL,
  0x03FFF8E0C180ULL, 0x03FFF8E0C180ULL, 0x03FFF8E0C180ULL,
  0x038038E0C180ULL, 0x018010E0C180ULL, 0x000000E0C180ULL,
  0x0FFFFCE0C180ULL, 0x0FFFFEE1C180ULL, 0x0FFFFEE1C180ULL,
  0x000700E1C180ULL, 0x000700E1C180ULL, 0x01C700E1C180ULL,
  0x01C700E1C180ULL, 0x01C7FC63E180ULL, 0x0187FC03F000ULL,
  0x0387FC07F800ULL, 0x03C7000F3E00ULL, 0x03C7001E1F00ULL,
  0x03E7007C0780ULL, 0x03F701F803C0ULL, 0x073F03E001C0ULL,
  0x073F01C00080ULL, 0x0F0FC0000000ULL, 0x0E07FFFFFFF0ULL,
  0x0E01FFFFFFF0ULL, 0x04001FFFFFE0ULL, 0x040000000000ULL
};

static const uint64_t glyph_er[48U] =
{
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
  0x000000000000ULL, 0x01FFFFFFFF80ULL, 0x01FFFFFFFF80ULL,
  0x01FFFFFFFF80ULL, 0x00FFFFFFFF00ULL, 0x000000000000ULL,
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
  0x07FFFFFFFFE0ULL, 0x0FFFFFFFFFF0ULL, 0x0FFFFFFFFFF0ULL,
  0x0FFFFFFFFFF0ULL, 0x000000000000ULL, 0x000000000000ULL,
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL
};

static const uint64_t glyph_san[48U] =
{
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
  0x000000000000ULL, 0x000000000000ULL, 0x01FFFFFFFF80ULL,
  0x01FFFFFFFF80ULL, 0x01FFFFFFFF80ULL, 0x01FFFFFFFF80ULL,
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
  0x000000000000ULL, 0x007FFFFFFE00ULL, 0x007FFFFFFE00ULL,
  0x007FFFFFFE00ULL, 0x007FFFFFFE00ULL, 0x000000000000ULL,
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
  0x07FFFFFFFFE0ULL, 0x0FFFFFFFFFF0ULL, 0x0FFFFFFFFFF0ULL,
  0x0FFFFFFFFFF0ULL, 0x000000000000ULL, 0x000000000000ULL,
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL
};

static const uint64_t glyph_wu[48U] =
{
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
  0x000000000000ULL, 0x03FFFFFFFFC0ULL, 0x07FFFFFFFFE0ULL,
  0x07FFFFFFFFE0ULL, 0x03FFFFFFFFC0ULL, 0x000038000000ULL,
  0x000078000000ULL, 0x000078000000ULL, 0x000078000000ULL,
  0x000070000000ULL, 0x000070000000ULL, 0x000070000000ULL,
  0x000070000000ULL, 0x0000F0000000ULL, 0x01FFFFFFF800ULL,
  0x03FFFFFFFC00ULL, 0x03FFFFFFFC00ULL, 0x01FFFFFFFC00ULL,
  0x0000E0003C00ULL, 0x0001E0003C00ULL, 0x0001E0003C00ULL,
  0x0001E0003C00ULL, 0x0001C0003C00ULL, 0x0001C0003C00ULL,
  0x0001C0003C00ULL, 0x0003C0003C00ULL, 0x0003C0003C00ULL,
  0x0003C0003C00ULL, 0x0003C0003C00ULL, 0x000380003C00ULL,
  0x000380003C00ULL, 0x000380003C00ULL, 0x000780003C00ULL,
  0x0FFFFFFFFFF0ULL, 0x0FFFFFFFFFF0ULL, 0x0FFFFFFFFFF0ULL,
  0x0FFFFFFFFFF0ULL, 0x000000000000ULL, 0x000000000000ULL
};

static const uint64_t glyph_si[48U] =
{
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
  0x000000000000ULL, 0x01FFFFFFFFC0ULL, 0x01FFFFFFFFC0ULL,
  0x01FFFFFFFFC0ULL, 0x01FFFFFFFFC0ULL, 0x01C03C1E01C0ULL,
  0x01C03C1E01C0ULL, 0x01C03C1E01C0ULL, 0x01C03C1E01C0ULL,
  0x01C0381E01C0ULL, 0x01C0381E01C0ULL, 0x01C0381E01C0ULL,
  0x01C0381E01C0ULL, 0x01C0381E01C0ULL, 0x01C0381E01C0ULL,
  0x01C0781E01C0ULL, 0x01C0781E01C0ULL, 0x01C0701E01C0ULL,
  0x01C0F01E01C0ULL, 0x01C0F01E01C0ULL, 0x01C1E00FFDC0ULL,
  0x01C3C00FFDC0ULL, 0x01C7C007FDC0ULL, 0x01DF8000F9C0ULL,
  0x01FF000001C0ULL, 0x01CC000001C0ULL, 0x01C0000001C0ULL,
  0x01C0000001C0ULL, 0x01C0000001C0ULL, 0x01C0000001C0ULL,
  0x01C0000001C0ULL, 0x01FFFFFFFFC0ULL, 0x01FFFFFFFFC0ULL,
  0x01FFFFFFFFC0ULL, 0x01C0000001C0ULL, 0x01C0000001C0ULL,
  0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL
};

/* 24x32 Latin glyphs rendered at 2x for the divider value. */
static const uint32_t glyph_ascii_8[32U] =
{
  0x000000UL, 0x000000UL, 0x000000UL, 0x000000UL,
  0x000000UL, 0x007E00UL, 0x00FF00UL, 0x01FF80UL,
  0x0381C0UL, 0x0381C0UL, 0x0381C0UL, 0x0381C0UL,
  0x01C380UL, 0x00FF00UL, 0x00FF00UL, 0x01FF80UL,
  0x0381C0UL, 0x0700E0UL, 0x0700E0UL, 0x0700E0UL,
  0x0700E0UL, 0x0380C0UL, 0x03C3C0UL, 0x01FF80UL,
  0x00FF00UL, 0x001800UL, 0x000000UL, 0x000000UL,
  0x000000UL, 0x000000UL, 0x000000UL, 0x000000UL
};
static const uint32_t glyph_ascii_6[32U] =
{
  0x000000UL, 0x000000UL, 0x000000UL, 0x000000UL,
  0x000000UL, 0x003E00UL, 0x00FF80UL, 0x01FF80UL,
  0x01C1C0UL, 0x0380C0UL, 0x0300C0UL, 0x070000UL,
  0x073E00UL, 0x077F80UL, 0x07FFC0UL, 0x0781C0UL,
  0x0780E0UL, 0x0700E0UL, 0x0700E0UL, 0x0700E0UL,
  0x0300E0UL, 0x0381C0UL, 0x01C3C0UL, 0x01FF80UL,
  0x007F00UL, 0x000800UL, 0x000000UL, 0x000000UL,
  0x000000UL, 0x000000UL, 0x000000UL, 0x000000UL
};
static const uint32_t glyph_ascii_4[32U] =
{
  0x000000UL, 0x000000UL, 0x000000UL, 0x000000UL,
  0x000000UL, 0x000300UL, 0x000700UL, 0x000F00UL,
  0x001F00UL, 0x001F00UL, 0x003700UL, 0x007700UL,
  0x00E700UL, 0x00C700UL, 0x018700UL, 0x038700UL,
  0x030700UL, 0x060700UL, 0x0FFFE0UL, 0x0FFFE0UL,
  0x07FFC0UL, 0x000700UL, 0x000700UL, 0x000700UL,
  0x000700UL, 0x000300UL, 0x000000UL, 0x000000UL,
  0x000000UL, 0x000000UL, 0x000000UL, 0x000000UL
};
static const uint32_t glyph_ascii_2[32U] =
{
  0x000000UL, 0x000000UL, 0x000000UL, 0x000000UL,
  0x000000UL, 0x007E00UL, 0x00FF80UL, 0x01FFC0UL,
  0x0381C0UL, 0x0300C0UL, 0x0700E0UL, 0x0000E0UL,
  0x0000C0UL, 0x0001C0UL, 0x000380UL, 0x000700UL,
  0x000F00UL, 0x001C00UL, 0x007800UL, 0x00F000UL,
  0x01E000UL, 0x03C000UL, 0x038000UL, 0x07FFE0UL,
  0x07FFE0UL, 0x07FFC0UL, 0x000000UL, 0x000000UL,
  0x000000UL, 0x000000UL, 0x000000UL, 0x000000UL
};
static const uint32_t glyph_ascii_d[32U] =
{
  0x000000UL, 0x000000UL, 0x000000UL, 0x000000UL,
  0x0000C0UL, 0x0001C0UL, 0x0001C0UL, 0x0001C0UL,
  0x0001C0UL, 0x0011C0UL, 0x00FDC0UL, 0x01FFC0UL,
  0x03C3C0UL, 0x0381C0UL, 0x0701C0UL, 0x0700C0UL,
  0x0700C0UL, 0x0700C0UL, 0x0700C0UL, 0x0700C0UL,
  0x0701C0UL, 0x0301C0UL, 0x0383C0UL, 0x01FFC0UL,
  0x00FEC0UL, 0x007CC0UL, 0x000000UL, 0x000000UL,
  0x000000UL, 0x000000UL, 0x000000UL, 0x000000UL
};
static const uint32_t glyph_ascii_i[32U] =
{
  0x000000UL, 0x000000UL, 0x000000UL, 0x000000UL,
  0x001800UL, 0x001C00UL, 0x001C00UL, 0x000000UL,
  0x000000UL, 0x000000UL, 0x001C00UL, 0x001C00UL,
  0x001C00UL, 0x001C00UL, 0x001C00UL, 0x001C00UL,
  0x001C00UL, 0x001C00UL, 0x001C00UL, 0x001C00UL,
  0x001C00UL, 0x001C00UL, 0x001800UL, 0x000000UL,
  0x000000UL, 0x000000UL, 0x000000UL, 0x000000UL,
  0x000000UL, 0x000000UL, 0x000000UL, 0x000000UL
};
static const uint32_t glyph_ascii_v[32U] =
{
  0x000000UL, 0x000000UL, 0x000000UL, 0x000000UL,
  0x000000UL, 0x000000UL, 0x000000UL, 0x000000UL,
  0x000000UL, 0x000000UL, 0x0300E0UL, 0x0380E0UL,
  0x0380C0UL, 0x0181C0UL, 0x01C1C0UL, 0x01C180UL,
  0x00C380UL, 0x00E300UL, 0x00E300UL, 0x006700UL,
  0x007600UL, 0x003600UL, 0x003E00UL, 0x003C00UL,
  0x001C00UL, 0x001800UL, 0x000000UL, 0x000000UL,
  0x000000UL, 0x000000UL, 0x000000UL, 0x000000UL
};
static const uint32_t glyph_ascii_multiply[32U] =
{
  0x000000UL, 0x000000UL, 0x000000UL, 0x000000UL,
  0x000000UL, 0x000000UL, 0x000000UL, 0x000000UL,
  0x000000UL, 0x010080UL, 0x0381C0UL, 0x03C3C0UL,
  0x01E780UL, 0x00FF00UL, 0x007E00UL, 0x003C00UL,
  0x007E00UL, 0x00FF00UL, 0x01E780UL, 0x0383C0UL,
  0x010180UL, 0x000000UL, 0x000000UL, 0x000000UL,
  0x000000UL, 0x000000UL, 0x000000UL, 0x000000UL,
  0x000000UL, 0x000000UL, 0x000000UL, 0x000000UL
};

static void TFT_WriteCommand(uint16_t command)
{
  TFT_COMMAND = command;
}

static void TFT_WriteData(uint16_t data)
{
  TFT_DATA = data;
}

static void TFT_GPIO_Init(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();

  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_15, GPIO_PIN_RESET);
  gpio.Pin = GPIO_PIN_15;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &gpio);

  gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5 |
             GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 |
             GPIO_PIN_11 | GPIO_PIN_14 | GPIO_PIN_15;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF12_FSMC;
  HAL_GPIO_Init(GPIOD, &gpio);

  gpio.Pin = GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 |
             GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 |
             GPIO_PIN_15;
  HAL_GPIO_Init(GPIOE, &gpio);
}

static void TFT_FSMC_Init(void)
{
  __HAL_RCC_FSMC_CLK_ENABLE();

  FSMC_Bank1->BTCR[0] = 0U;
  FSMC_Bank1->BTCR[1] = (15U << FSMC_BTR1_ADDSET_Pos) |
                         (60U << FSMC_BTR1_DATAST_Pos);
  FSMC_Bank1E->BWTR[0] = (9U << FSMC_BWTR1_ADDSET_Pos) |
                          (8U << FSMC_BWTR1_DATAST_Pos);
  FSMC_Bank1->BTCR[0] = FSMC_BCR1_WREN | FSMC_BCR1_EXTMOD |
                         FSMC_BCR1_MWID_0 | FSMC_BCR1_MBKEN;
}

static void TFT_Controller_Init(void)
{
  static const uint8_t positive_gamma[15] =
  {
    0x00U, 0x07U, 0x0FU, 0x0DU, 0x1BU, 0x0AU, 0x3CU, 0x78U,
    0x4AU, 0x07U, 0x0EU, 0x09U, 0x1BU, 0x1EU, 0x0FU
  };
  static const uint8_t negative_gamma[15] =
  {
    0x00U, 0x22U, 0x24U, 0x06U, 0x12U, 0x07U, 0x36U, 0x47U,
    0x47U, 0x06U, 0x0AU, 0x07U, 0x30U, 0x37U, 0x0FU
  };
  uint32_t i;

  TFT_WriteCommand(0xE0U);
  for (i = 0U; i < 15U; i++)
    TFT_WriteData(positive_gamma[i]);

  TFT_WriteCommand(0xE1U);
  for (i = 0U; i < 15U; i++)
    TFT_WriteData(negative_gamma[i]);

  TFT_WriteCommand(0xC0U);
  TFT_WriteData(0x10U);
  TFT_WriteData(0x10U);
  TFT_WriteCommand(0xC1U);
  TFT_WriteData(0x41U);
  TFT_WriteCommand(0xC5U);
  TFT_WriteData(0x00U);
  TFT_WriteData(0x22U);
  TFT_WriteData(0x80U);
  TFT_WriteCommand(0x36U);
  /* ILI9486 landscape: MV + BGR. 0xA8 also sets MY and mirrors the image. */
  TFT_WriteData(0x28U);
  TFT_WriteCommand(0x3AU);
  TFT_WriteData(0x55U);
  TFT_WriteCommand(0xB0U);
  TFT_WriteData(0x00U);
  TFT_WriteCommand(0xB1U);
  TFT_WriteData(0xB0U);
  TFT_WriteData(0x11U);
  TFT_WriteCommand(0xB4U);
  TFT_WriteData(0x02U);
  TFT_WriteCommand(0xB6U);
  TFT_WriteData(0x02U);
  TFT_WriteData(0x02U);
  TFT_WriteCommand(0xB7U);
  TFT_WriteData(0xC6U);
  TFT_WriteCommand(0xE9U);
  TFT_WriteData(0x00U);
  TFT_WriteCommand(0xF7U);
  TFT_WriteData(0xA9U);
  TFT_WriteData(0x51U);
  TFT_WriteData(0x2CU);
  TFT_WriteData(0x82U);
  TFT_WriteCommand(0x11U);
  HAL_Delay(120U);
  TFT_WriteCommand(0x29U);
}

static void TFT_SetWindow(uint16_t x, uint16_t y,
                          uint16_t width, uint16_t height)
{
  uint16_t end_x = (uint16_t)(x + width - 1U);
  uint16_t end_y = (uint16_t)(y + height - 1U);

  TFT_WriteCommand(0x2AU);
  TFT_WriteData((uint16_t)(x >> 8));
  TFT_WriteData((uint16_t)(x & 0xFFU));
  TFT_WriteData((uint16_t)(end_x >> 8));
  TFT_WriteData((uint16_t)(end_x & 0xFFU));
  TFT_WriteCommand(0x2BU);
  TFT_WriteData((uint16_t)(y >> 8));
  TFT_WriteData((uint16_t)(y & 0xFFU));
  TFT_WriteData((uint16_t)(end_y >> 8));
  TFT_WriteData((uint16_t)(end_y & 0xFFU));
  TFT_WriteCommand(0x2CU);
}

static void TFT_Clear(uint16_t color)
{
  uint32_t i;

  TFT_SetWindow(0U, 0U, TFT_WIDTH, TFT_HEIGHT);
  for (i = 0U; i < (uint32_t)TFT_WIDTH * TFT_HEIGHT; i++)
    TFT_DATA = color;
}

static void TFT_FillRectangle(uint16_t x, uint16_t y,
                              uint16_t width, uint16_t height,
                              uint16_t color)
{
  uint32_t i;

  TFT_SetWindow(x, y, width, height);
  for (i = 0U; i < (uint32_t)width * height; i++)
    TFT_DATA = color;
}

static void TFT_DrawChinese(uint16_t x, uint16_t y,
                            const uint64_t glyph[48], uint8_t scale,
                            uint16_t color, uint16_t background)
{
  uint8_t source_x;
  uint8_t source_y;
  uint8_t repeat_x;
  uint8_t repeat_y;
  uint16_t pixel;

  TFT_SetWindow(x, y, (uint16_t)(48U * scale), (uint16_t)(48U * scale));
  for (source_y = 0U; source_y < 48U; source_y++)
  {
    for (repeat_y = 0U; repeat_y < scale; repeat_y++)
    {
      for (source_x = 0U; source_x < 48U; source_x++)
      {
        pixel = (((glyph[source_y] >> (47U - source_x)) & 1ULL) != 0U) ?
                    color : background;
        for (repeat_x = 0U; repeat_x < scale; repeat_x++)
          TFT_DATA = pixel;
      }
    }
  }
}

static const uint32_t *TFT_GetAsciiGlyph(uint8_t character)
{
  switch (character)
  {
    case '8': return glyph_ascii_8;
    case '6': return glyph_ascii_6;
    case '4': return glyph_ascii_4;
    case '2': return glyph_ascii_2;
    case 'd': return glyph_ascii_d;
    case 'i': return glyph_ascii_i;
    case 'v': return glyph_ascii_v;
    case TFT_MULTIPLY_CHAR: return glyph_ascii_multiply;
    default: return glyph_ascii_8;
  }
}

static void TFT_DrawAscii(uint16_t x, uint16_t y, uint8_t character,
                          uint8_t scale, uint16_t color,
                          uint16_t background)
{
  uint8_t source_x;
  uint8_t source_y;
  uint8_t repeat_x;
  uint8_t repeat_y;
  const uint32_t *glyph;
  uint16_t pixel;

  glyph = TFT_GetAsciiGlyph(character);
  TFT_SetWindow(x, y, (uint16_t)(24U * scale), (uint16_t)(32U * scale));
  for (source_y = 0U; source_y < 32U; source_y++)
  {
    for (repeat_y = 0U; repeat_y < scale; repeat_y++)
    {
      for (source_x = 0U; source_x < 24U; source_x++)
      {
        pixel = ((glyph[source_y] & (0x00800000UL >> source_x)) != 0U) ?
                    color : background;
        for (repeat_x = 0U; repeat_x < scale; repeat_x++)
          TFT_DATA = pixel;
      }
    }
  }
}

/* Compact 5x7 glyphs used by the variable-width vision frequency row. */
static const uint8_t glyph_small_0[7U] =
    {0x0EU, 0x11U, 0x13U, 0x15U, 0x19U, 0x11U, 0x0EU};
static const uint8_t glyph_small_1[7U] =
    {0x04U, 0x0CU, 0x04U, 0x04U, 0x04U, 0x04U, 0x0EU};
static const uint8_t glyph_small_2[7U] =
    {0x0EU, 0x11U, 0x01U, 0x02U, 0x04U, 0x08U, 0x1FU};
static const uint8_t glyph_small_3[7U] =
    {0x1EU, 0x01U, 0x01U, 0x0EU, 0x01U, 0x01U, 0x1EU};
static const uint8_t glyph_small_4[7U] =
    {0x02U, 0x06U, 0x0AU, 0x12U, 0x1FU, 0x02U, 0x02U};
static const uint8_t glyph_small_5[7U] =
    {0x1FU, 0x10U, 0x10U, 0x1EU, 0x01U, 0x01U, 0x1EU};
static const uint8_t glyph_small_6[7U] =
    {0x0EU, 0x10U, 0x10U, 0x1EU, 0x11U, 0x11U, 0x0EU};
static const uint8_t glyph_small_7[7U] =
    {0x1FU, 0x01U, 0x02U, 0x04U, 0x08U, 0x08U, 0x08U};
static const uint8_t glyph_small_8[7U] =
    {0x0EU, 0x11U, 0x11U, 0x0EU, 0x11U, 0x11U, 0x0EU};
static const uint8_t glyph_small_9[7U] =
    {0x0EU, 0x11U, 0x11U, 0x0FU, 0x01U, 0x01U, 0x0EU};
static const uint8_t glyph_small_f[7U] =
    {0x1FU, 0x10U, 0x10U, 0x1EU, 0x10U, 0x10U, 0x10U};
static const uint8_t glyph_small_h[7U] =
    {0x11U, 0x11U, 0x11U, 0x1FU, 0x11U, 0x11U, 0x11U};
static const uint8_t glyph_small_z[7U] =
    {0x00U, 0x00U, 0x1FU, 0x02U, 0x04U, 0x08U, 0x1FU};
static const uint8_t glyph_small_colon[7U] =
    {0x00U, 0x04U, 0x04U, 0x00U, 0x04U, 0x04U, 0x00U};
static const uint8_t glyph_small_period[7U] =
    {0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x0CU, 0x0CU};
static const uint8_t glyph_small_blank[7U] =
    {0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U};

static const uint8_t *TFT_GetSmallGlyph(uint8_t character)
{
  switch (character)
  {
    case '0': return glyph_small_0;
    case '1': return glyph_small_1;
    case '2': return glyph_small_2;
    case '3': return glyph_small_3;
    case '4': return glyph_small_4;
    case '5': return glyph_small_5;
    case '6': return glyph_small_6;
    case '7': return glyph_small_7;
    case '8': return glyph_small_8;
    case '9': return glyph_small_9;
    case 'F': return glyph_small_f;
    case 'H': return glyph_small_h;
    case 'z': return glyph_small_z;
    case ':': return glyph_small_colon;
    case '.': return glyph_small_period;
    default: return glyph_small_blank;
  }
}

static void TFT_DrawSmallAscii(uint16_t x, uint16_t y, uint8_t character,
                               uint8_t scale, uint16_t color,
                               uint16_t background)
{
  const uint8_t *glyph = TFT_GetSmallGlyph(character);
  uint8_t source_x;
  uint8_t source_y;
  uint8_t repeat_x;
  uint8_t repeat_y;
  uint16_t pixel;

  TFT_SetWindow(x, y, (uint16_t)(5U * scale), (uint16_t)(7U * scale));
  for (source_y = 0U; source_y < 7U; source_y++)
  {
    for (repeat_y = 0U; repeat_y < scale; repeat_y++)
    {
      for (source_x = 0U; source_x < 5U; source_x++)
      {
        pixel = ((glyph[source_y] & (0x10U >> source_x)) != 0U) ?
                    color : background;
        for (repeat_x = 0U; repeat_x < scale; repeat_x++)
          TFT_DATA = pixel;
      }
    }
  }
}

static uint8_t TFT_AppendUnsigned(uint8_t *text, uint8_t position,
                                  uint32_t value)
{
  uint8_t digits[10U];
  uint8_t count = 0U;

  do
  {
    digits[count++] = (uint8_t)('0' + value % 10U);
    value /= 10U;
  } while (value != 0U);

  while (count != 0U)
    text[position++] = digits[--count];
  return position;
}

static uint8_t TFT_FormatFrequency(uint32_t frequency_millihz,
                                   uint8_t text[20U])
{
  uint32_t fractional_millihz = frequency_millihz % 1000U;
  uint8_t position = 0U;

  text[position++] = 'F';
  text[position++] = ':';
  position = TFT_AppendUnsigned(text, position,
                                frequency_millihz / 1000U);
  text[position++] = '.';
  text[position++] = (uint8_t)('0' + fractional_millihz / 100U);
  text[position++] = (uint8_t)('0' + (fractional_millihz / 10U) % 10U);
  text[position++] = (uint8_t)('0' + fractional_millihz % 10U);
  text[position++] = 'H';
  text[position++] = 'z';
  return position;
}

static void TFT_DrawVisionFrequency(uint32_t frequency_millihz)
{
  uint8_t text[20U];
  uint8_t length;
  uint8_t index;
  uint16_t advance = (uint16_t)(6U * TFT_VISION_FONT_SCALE);
  uint16_t width;
  uint16_t x;

  length = TFT_FormatFrequency(frequency_millihz, text);
  width = (uint16_t)((uint16_t)length * advance -
                     TFT_VISION_FONT_SCALE);
  x = (width < TFT_WIDTH) ? (uint16_t)((TFT_WIDTH - width) / 2U) : 0U;
  for (index = 0U; index < length; index++)
  {
    TFT_DrawSmallAscii((uint16_t)(x + (uint16_t)index * advance),
                       TFT_VISION_VALUE_Y, text[index],
                       TFT_VISION_FONT_SCALE, TFT_VALUE_COLOR,
                       TFT_BACKGROUND_COLOR);
  }
}

static void TFT_DrawQuestionText(const uint64_t middle_glyph[48],
                                 uint8_t scale, uint16_t x, uint16_t y,
                                 uint16_t gap)
{
  uint16_t advance = (uint16_t)(48U * scale + gap);

  TFT_DrawChinese(x, y, glyph_di, scale, TFT_TEXT_COLOR,
                  TFT_BACKGROUND_COLOR);
  TFT_DrawChinese((uint16_t)(x + advance), y, middle_glyph, scale,
                  TFT_TEXT_COLOR, TFT_BACKGROUND_COLOR);
  TFT_DrawChinese((uint16_t)(x + 2U * advance), y, glyph_ti, scale,
                  TFT_TEXT_COLOR, TFT_BACKGROUND_COLOR);
}

void TFT_Display_Init(void)
{
  TFT_GPIO_Init();
  TFT_FSMC_Init();
  HAL_Delay(20U);
  TFT_Controller_Init();
  TFT_Clear(TFT_BACKGROUND_COLOR);
}

void TFT_Display_ShowQuestion(uint8_t question_number)
{
  const uint64_t *middle_glyph;

  if (question_number == 2U)
    middle_glyph = glyph_er;
  else if (question_number == 3U)
    middle_glyph = glyph_san;
  else if (question_number == 5U)
    middle_glyph = glyph_wu;
  else
    middle_glyph = glyph_yi;

  TFT_Clear(TFT_BACKGROUND_COLOR);
  TFT_DrawQuestionText(middle_glyph, 2U, 76U, 112U, 20U);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_15, GPIO_PIN_SET);
}

void TFT_Display_ShowFourth(uint8_t vertical_divisions)
{
  uint8_t suffix[] =
  {
    '8', TFT_MULTIPLY_CHAR, '8', 'd', 'i', 'v'
  };
  uint8_t i;

  if (vertical_divisions != 8U && vertical_divisions != 6U &&
      vertical_divisions != 4U && vertical_divisions != 2U)
    vertical_divisions = 8U;

  suffix[2] = (uint8_t)('0' + vertical_divisions);
  TFT_Clear(TFT_BACKGROUND_COLOR);
  TFT_DrawQuestionText(glyph_si, 1U, 156U, 54U, 12U);
  for (i = 0U; i < 6U; i++)
  {
    TFT_DrawAscii((uint16_t)(100U + (uint16_t)i * 48U), 202U,
                  suffix[i], 2U, TFT_VALUE_COLOR, TFT_BACKGROUND_COLOR);
  }
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_15, GPIO_PIN_SET);
}

void TFT_Display_ShowVision(uint32_t frequency_millihz)
{
  TFT_Clear(TFT_BACKGROUND_COLOR);
  TFT_DrawQuestionText(glyph_wu, 1U, 156U, 54U, 12U);
  TFT_DrawVisionFrequency(frequency_millihz);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_15, GPIO_PIN_SET);
}

void TFT_Display_UpdateVisionFrequency(uint32_t frequency_millihz)
{
  TFT_FillRectangle(0U, TFT_VISION_VALUE_Y, TFT_WIDTH,
                    TFT_VISION_VALUE_H, TFT_BACKGROUND_COLOR);
  TFT_DrawVisionFrequency(frequency_millihz);
}
