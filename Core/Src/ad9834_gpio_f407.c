/**
  ******************************************************************************
  * @file    ad9834_gpio_f407.c
  * @brief   AD9834 GPIO 模拟串行驱动实现。
  *
  * 串行时序和寄存器定义依据资料包内 AD9834 手册及模块示例整理。
  * AD9834 不提供数字调幅寄存器，幅度必须由 RSET/FS ADJUST 或外部增益级控制。
  ******************************************************************************
  */

#include "ad9834_gpio_f407.h"

#define AD9834_CTRL_B28        0x2000U
#define AD9834_CTRL_HLB        0x1000U
#define AD9834_CTRL_FSEL       0x0800U
#define AD9834_CTRL_PSEL       0x0400U
#define AD9834_CTRL_PIN_SW     0x0200U
#define AD9834_CTRL_RESET      0x0100U
#define AD9834_CTRL_SLEEP1     0x0080U
#define AD9834_CTRL_SLEEP12    0x0040U
#define AD9834_CTRL_OPBITEN    0x0020U
#define AD9834_CTRL_SIGN_PIB   0x0010U
#define AD9834_CTRL_DIV2       0x0008U
#define AD9834_CTRL_MODE       0x0002U

#define AD9834_FREQ0_ADDR      0x4000U
#define AD9834_FREQ1_ADDR      0x8000U
#define AD9834_PHASE0_ADDR     0xC000U
#define AD9834_PHASE1_ADDR     0xE000U
#define AD9834_FREQ_MASK       0x0FFFFFFFUL
#define AD9834_FREQ_14BIT_MASK 0x3FFFU
#define AD9834_PHASE_MASK      0x0FFFU

static uint32_t AD9834_GPIO_CalcFrequencyWordMilliHz(
    uint32_t frequency_millihz,
    uint32_t mclk_hz);

/* 把句柄中的控制寄存器镜像写入芯片。 */
static HAL_StatusTypeDef AD9834_GPIO_WriteControl(AD9834_GPIO_HandleTypeDef *dev);
/* 使能端口时钟并把单个接口引脚配置为上拉推挽输出。 */
static HAL_StatusTypeDef AD9834_GPIO_InitOnePin(GPIO_TypeDef *port, uint16_t pin);
/* 根据 GPIOA~GPIOK 端口地址打开对应 RCC 时钟。 */
static HAL_StatusTypeDef AD9834_GPIO_EnablePortClock(GPIO_TypeDef *port);
/* 检查句柄中的端口、引脚和 MCLK 参数是否完整。 */
static uint8_t AD9834_GPIO_IsValidHandle(const AD9834_GPIO_HandleTypeDef *dev);
/* GPIO 边沿之间的短空循环延时，不用于精确定时。 */
static void AD9834_GPIO_Delay(const AD9834_GPIO_HandleTypeDef *dev);

/* 使用本工程 PC0~PC3 和 75MHz MCLK 调用通用初始化函数。 */
HAL_StatusTypeDef AD9834_GPIO_InitDefault(AD9834_GPIO_HandleTypeDef *dev)
{
    return AD9834_GPIO_Init(dev,
                            AD9834_GPIO_DEFAULT_SDA_PORT,
                            AD9834_GPIO_DEFAULT_SDA_PIN,
                            AD9834_GPIO_DEFAULT_CLK_PORT,
                            AD9834_GPIO_DEFAULT_CLK_PIN,
                            AD9834_GPIO_DEFAULT_SYNC_PORT,
                            AD9834_GPIO_DEFAULT_SYNC_PIN,
                            AD9834_GPIO_DEFAULT_RESET_PORT,
                            AD9834_GPIO_DEFAULT_RESET_PIN,
                            AD9834_GPIO_DEFAULT_MCLK_HZ);
}

/*
 * 保存自定义接线，执行硬件 RESET，并初始化控制、PHASE0 和 FREQ0。
 * 初始化完成时频率为 0Hz，应用层可再选择是否保持软件 RESET。
 */
HAL_StatusTypeDef AD9834_GPIO_Init(AD9834_GPIO_HandleTypeDef *dev,
                                   GPIO_TypeDef *sda_port,
                                   uint16_t sda_pin,
                                   GPIO_TypeDef *clk_port,
                                   uint16_t clk_pin,
                                   GPIO_TypeDef *sync_port,
                                   uint16_t sync_pin,
                                   GPIO_TypeDef *reset_port,
                                   uint16_t reset_pin,
                                   uint32_t mclk_hz)
{
    HAL_StatusTypeDef status;

    if ((dev == 0) || (sda_port == 0) || (clk_port == 0) ||
        (sync_port == 0) || (reset_port == 0) ||
        (sda_pin == 0U) || (clk_pin == 0U) ||
        (sync_pin == 0U) || (reset_pin == 0U))
    {
        return HAL_ERROR;
    }

    dev->sda_port = sda_port;
    dev->sda_pin = sda_pin;
    dev->clk_port = clk_port;
    dev->clk_pin = clk_pin;
    dev->sync_port = sync_port;
    dev->sync_pin = sync_pin;
    dev->reset_port = reset_port;
    dev->reset_pin = reset_pin;
    dev->mclk_hz = (mclk_hz == 0U) ? AD9834_GPIO_DEFAULT_MCLK_HZ : mclk_hz;
    dev->bit_delay_cycles = AD9834_GPIO_DEFAULT_BIT_DELAY;
    dev->control = AD9834_CTRL_B28 | AD9834_CTRL_DIV2 | AD9834_CTRL_RESET;

    status = AD9834_GPIO_InitOnePin(dev->sda_port, dev->sda_pin);
    if (status != HAL_OK)
    {
        return status;
    }

    status = AD9834_GPIO_InitOnePin(dev->clk_port, dev->clk_pin);
    if (status != HAL_OK)
    {
        return status;
    }

    status = AD9834_GPIO_InitOnePin(dev->sync_port, dev->sync_pin);
    if (status != HAL_OK)
    {
        return status;
    }

    status = AD9834_GPIO_InitOnePin(dev->reset_port, dev->reset_pin);
    if (status != HAL_OK)
    {
        return status;
    }

    HAL_GPIO_WritePin(dev->sda_port, dev->sda_pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(dev->clk_port, dev->clk_pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(dev->sync_port, dev->sync_pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(dev->reset_port, dev->reset_pin, GPIO_PIN_SET);
    HAL_Delay(1U);
    HAL_GPIO_WritePin(dev->reset_port, dev->reset_pin, GPIO_PIN_RESET);
    HAL_Delay(100U);

    status = AD9834_GPIO_WriteControl(dev);
    if (status != HAL_OK)
    {
        return status;
    }

    status = AD9834_GPIO_WritePhaseWord(dev, AD9834_GPIO_PHASE_REG0, 0U);
    if (status != HAL_OK)
    {
        return status;
    }

    status = AD9834_GPIO_WriteFrequencyWord(dev, AD9834_GPIO_FREQ_REG0, 0U);
    if (status != HAL_OK)
    {
        return status;
    }

    dev->control &= (uint16_t)~AD9834_CTRL_RESET;
    return AD9834_GPIO_WriteControl(dev);
}

/* 调整模拟串行接口速度；飞线较长或边沿振铃时可增大 cycles。 */
void AD9834_GPIO_SetBitDelayCycles(AD9834_GPIO_HandleTypeDef *dev,
                                   uint32_t cycles)
{
    if (dev != 0)
    {
        dev->bit_delay_cycles = cycles;
    }
}

/* FSYNC 拉低后以 MSB first 发送一个 16 位 AD9834 寄存器字。 */
HAL_StatusTypeDef AD9834_GPIO_WriteRegister(AD9834_GPIO_HandleTypeDef *dev,
                                            uint16_t value)
{
    uint8_t i;

    if (!AD9834_GPIO_IsValidHandle(dev))
    {
        return HAL_ERROR;
    }

    HAL_GPIO_WritePin(dev->clk_port, dev->clk_pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(dev->sync_port, dev->sync_pin, GPIO_PIN_RESET);
    AD9834_GPIO_Delay(dev);

    for (i = 0U; i < 16U; i++)
    {
        if ((value & 0x8000U) != 0U)
        {
            HAL_GPIO_WritePin(dev->sda_port, dev->sda_pin, GPIO_PIN_SET);
        }
        else
        {
            HAL_GPIO_WritePin(dev->sda_port, dev->sda_pin, GPIO_PIN_RESET);
        }

        AD9834_GPIO_Delay(dev);
        HAL_GPIO_WritePin(dev->clk_port, dev->clk_pin, GPIO_PIN_RESET);
        AD9834_GPIO_Delay(dev);
        value <<= 1U;
        HAL_GPIO_WritePin(dev->clk_port, dev->clk_pin, GPIO_PIN_SET);
        AD9834_GPIO_Delay(dev);
    }

    HAL_GPIO_WritePin(dev->sda_port, dev->sda_pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(dev->sync_port, dev->sync_pin, GPIO_PIN_SET);

    return HAL_OK;
}

/* 在软件 RESET 期间更新波形和 FREQ0，配置完成后恢复输出。 */
HAL_StatusTypeDef AD9834_GPIO_SetOutput(AD9834_GPIO_HandleTypeDef *dev,
                                        uint32_t frequency_hz,
                                        AD9834_GPIO_Waveform waveform)
{
    HAL_StatusTypeDef status;

    status = AD9834_GPIO_SetSoftwareReset(dev, 1U);
    if (status != HAL_OK)
    {
        return status;
    }

    status = AD9834_GPIO_SetWaveform(dev, waveform);
    if (status != HAL_OK)
    {
        (void)AD9834_GPIO_SetSoftwareReset(dev, 0U);
        return status;
    }

    status = AD9834_GPIO_SetFrequencyHz(dev, frequency_hz);
    if (status != HAL_OK)
    {
        (void)AD9834_GPIO_SetSoftwareReset(dev, 0U);
        return status;
    }

    return AD9834_GPIO_SetSoftwareReset(dev, 0U);
}

HAL_StatusTypeDef AD9834_GPIO_SetOutputMilliHz(
    AD9834_GPIO_HandleTypeDef *dev,
    uint32_t frequency_millihz,
    AD9834_GPIO_Waveform waveform)
{
    HAL_StatusTypeDef status;

    status = AD9834_GPIO_SetSoftwareReset(dev, 1U);
    if (status != HAL_OK)
    {
        return status;
    }

    status = AD9834_GPIO_SetWaveform(dev, waveform);
    if (status != HAL_OK)
    {
        (void)AD9834_GPIO_SetSoftwareReset(dev, 0U);
        return status;
    }

    status = AD9834_GPIO_SetFrequencyMilliHz(dev, frequency_millihz);
    if (status != HAL_OK)
    {
        (void)AD9834_GPIO_SetSoftwareReset(dev, 0U);
        return status;
    }

    return AD9834_GPIO_SetSoftwareReset(dev, 0U);
}

/* 用 Hz 更新 FREQ0，并把 FSEL 切换到 FREQ0。 */
HAL_StatusTypeDef AD9834_GPIO_SetFrequencyHz(AD9834_GPIO_HandleTypeDef *dev,
                                             uint32_t frequency_hz)
{
    HAL_StatusTypeDef status;

    status = AD9834_GPIO_WriteFrequencyHz(dev, AD9834_GPIO_FREQ_REG0, frequency_hz);
    if (status != HAL_OK)
    {
        return status;
    }

    return AD9834_GPIO_SelectFrequencyRegister(dev, AD9834_GPIO_FREQ_REG0);
}

HAL_StatusTypeDef AD9834_GPIO_SetFrequencyMilliHz(
    AD9834_GPIO_HandleTypeDef *dev,
    uint32_t frequency_millihz)
{
    if (!AD9834_GPIO_IsValidHandle(dev) ||
        (uint64_t)frequency_millihz > ((uint64_t)dev->mclk_hz * 500U))
    {
        return HAL_ERROR;
    }

    if (AD9834_GPIO_WriteFrequencyWord(
            dev,
            AD9834_GPIO_FREQ_REG0,
            AD9834_GPIO_CalcFrequencyWordMilliHz(frequency_millihz,
                                                  dev->mclk_hz)) != HAL_OK)
    {
        return HAL_ERROR;
    }

    return AD9834_GPIO_SelectFrequencyRegister(dev, AD9834_GPIO_FREQ_REG0);
}

/* 将 Hz 换算成 28 位频率字后写入指定寄存器，但不改变 FSEL。 */
HAL_StatusTypeDef AD9834_GPIO_WriteFrequencyHz(AD9834_GPIO_HandleTypeDef *dev,
                                               AD9834_GPIO_FreqReg reg,
                                               uint32_t frequency_hz)
{
    if (!AD9834_GPIO_IsValidHandle(dev) || (frequency_hz > (dev->mclk_hz / 2U)))
    {
        return HAL_ERROR;
    }

    return AD9834_GPIO_WriteFrequencyWord(dev,
                                          reg,
                                          AD9834_GPIO_CalcFrequencyWord(frequency_hz,
                                                                       dev->mclk_hz));
}

/* 以 B28 模式依次写入频率字低 14 位和高 14 位。 */
HAL_StatusTypeDef AD9834_GPIO_WriteFrequencyWord(AD9834_GPIO_HandleTypeDef *dev,
                                                 AD9834_GPIO_FreqReg reg,
                                                 uint32_t frequency_word)
{
    HAL_StatusTypeDef status;
    uint16_t addr;
    uint16_t low_word;
    uint16_t high_word;

    if (!AD9834_GPIO_IsValidHandle(dev) ||
        ((reg != AD9834_GPIO_FREQ_REG0) && (reg != AD9834_GPIO_FREQ_REG1)))
    {
        return HAL_ERROR;
    }

    if ((dev->control & AD9834_CTRL_B28) == 0U)
    {
        dev->control |= AD9834_CTRL_B28;
        status = AD9834_GPIO_WriteControl(dev);
        if (status != HAL_OK)
        {
            return status;
        }
    }

    frequency_word &= AD9834_FREQ_MASK;
    addr = (reg == AD9834_GPIO_FREQ_REG0) ? AD9834_FREQ0_ADDR : AD9834_FREQ1_ADDR;
    low_word = (uint16_t)(addr | (frequency_word & AD9834_FREQ_14BIT_MASK));
    high_word = (uint16_t)(addr | ((frequency_word >> 14U) & AD9834_FREQ_14BIT_MASK));

    status = AD9834_GPIO_WriteRegister(dev, low_word);
    if (status != HAL_OK)
    {
        return status;
    }

    return AD9834_GPIO_WriteRegister(dev, high_word);
}

/* 通过控制寄存器 FSEL 位选择 FREQ0/FREQ1。 */
HAL_StatusTypeDef AD9834_GPIO_SelectFrequencyRegister(AD9834_GPIO_HandleTypeDef *dev,
                                                      AD9834_GPIO_FreqReg reg)
{
    if (!AD9834_GPIO_IsValidHandle(dev) ||
        ((reg != AD9834_GPIO_FREQ_REG0) && (reg != AD9834_GPIO_FREQ_REG1)))
    {
        return HAL_ERROR;
    }

    if (reg == AD9834_GPIO_FREQ_REG1)
    {
        dev->control |= AD9834_CTRL_FSEL;
    }
    else
    {
        dev->control &= (uint16_t)~AD9834_CTRL_FSEL;
    }

    return AD9834_GPIO_WriteControl(dev);
}

/* 写入 PHASE0/PHASE1 的 12 位原始相位字，不改变 PSEL。 */
HAL_StatusTypeDef AD9834_GPIO_WritePhaseWord(AD9834_GPIO_HandleTypeDef *dev,
                                             AD9834_GPIO_PhaseReg reg,
                                             uint16_t phase_word)
{
    uint16_t addr;

    if (!AD9834_GPIO_IsValidHandle(dev) ||
        ((reg != AD9834_GPIO_PHASE_REG0) && (reg != AD9834_GPIO_PHASE_REG1)))
    {
        return HAL_ERROR;
    }

    addr = (reg == AD9834_GPIO_PHASE_REG0) ? AD9834_PHASE0_ADDR : AD9834_PHASE1_ADDR;
    return AD9834_GPIO_WriteRegister(dev, (uint16_t)(addr | (phase_word & AD9834_PHASE_MASK)));
}

/* 将角度按 360 度取模并换算为 0~4095 相位字。 */
HAL_StatusTypeDef AD9834_GPIO_WritePhaseDegree(AD9834_GPIO_HandleTypeDef *dev,
                                               AD9834_GPIO_PhaseReg reg,
                                               uint16_t degree)
{
    uint32_t phase_word;

    degree %= 360U;
    phase_word = (((uint32_t)degree * 4096U) + 180U) / 360U;

    return AD9834_GPIO_WritePhaseWord(dev, reg, (uint16_t)phase_word);
}

/* 通过控制寄存器 PSEL 位选择 PHASE0/PHASE1。 */
HAL_StatusTypeDef AD9834_GPIO_SelectPhaseRegister(AD9834_GPIO_HandleTypeDef *dev,
                                                  AD9834_GPIO_PhaseReg reg)
{
    if (!AD9834_GPIO_IsValidHandle(dev) ||
        ((reg != AD9834_GPIO_PHASE_REG0) && (reg != AD9834_GPIO_PHASE_REG1)))
    {
        return HAL_ERROR;
    }

    if (reg == AD9834_GPIO_PHASE_REG1)
    {
        dev->control |= AD9834_CTRL_PSEL;
    }
    else
    {
        dev->control &= (uint16_t)~AD9834_CTRL_PSEL;
    }

    return AD9834_GPIO_WriteControl(dev);
}

/* 根据枚举组合 MODE、OPBITEN、SIGN/PIB 和 DIV2 控制位。 */
HAL_StatusTypeDef AD9834_GPIO_SetWaveform(AD9834_GPIO_HandleTypeDef *dev,
                                          AD9834_GPIO_Waveform waveform)
{
    if (!AD9834_GPIO_IsValidHandle(dev))
    {
        return HAL_ERROR;
    }

    dev->control &= (uint16_t)~(AD9834_CTRL_MODE |
                               AD9834_CTRL_OPBITEN |
                               AD9834_CTRL_SIGN_PIB |
                               AD9834_CTRL_DIV2);

    switch (waveform)
    {
        case AD9834_GPIO_WAVE_SINE:
            dev->control |= AD9834_CTRL_DIV2;
            break;

        case AD9834_GPIO_WAVE_TRIANGLE:
            dev->control |= AD9834_CTRL_MODE;
            break;

        case AD9834_GPIO_WAVE_SQUARE:
            dev->control |= (AD9834_CTRL_OPBITEN | AD9834_CTRL_DIV2);
            break;

        case AD9834_GPIO_WAVE_SQUARE_DIV2:
            dev->control |= AD9834_CTRL_OPBITEN;
            break;

        case AD9834_GPIO_WAVE_SQUARE_COMPARATOR:
            dev->control |= (AD9834_CTRL_OPBITEN |
                             AD9834_CTRL_SIGN_PIB |
                             AD9834_CTRL_DIV2);
            break;

        default:
            return HAL_ERROR;
    }

    return AD9834_GPIO_WriteControl(dev);
}

/* 修改控制寄存器 RESET 位；不会翻转 PC3 硬件 RESET 引脚。 */
HAL_StatusTypeDef AD9834_GPIO_SetSoftwareReset(AD9834_GPIO_HandleTypeDef *dev,
                                               uint8_t enable)
{
    if (!AD9834_GPIO_IsValidHandle(dev))
    {
        return HAL_ERROR;
    }

    if (enable != 0U)
    {
        dev->control |= AD9834_CTRL_RESET;
    }
    else
    {
        dev->control &= (uint16_t)~AD9834_CTRL_RESET;
    }

    return AD9834_GPIO_WriteControl(dev);
}

/* 通过 SLEEP12/SLEEP1 分别关闭 DAC 和内部 MCLK。 */
HAL_StatusTypeDef AD9834_GPIO_SetSleep(AD9834_GPIO_HandleTypeDef *dev,
                                       uint8_t sleep_dac,
                                       uint8_t sleep_mclk)
{
    if (!AD9834_GPIO_IsValidHandle(dev))
    {
        return HAL_ERROR;
    }

    if (sleep_dac != 0U)
    {
        dev->control |= AD9834_CTRL_SLEEP12;
    }
    else
    {
        dev->control &= (uint16_t)~AD9834_CTRL_SLEEP12;
    }

    if (sleep_mclk != 0U)
    {
        dev->control |= AD9834_CTRL_SLEEP1;
    }
    else
    {
        dev->control &= (uint16_t)~AD9834_CTRL_SLEEP1;
    }

    return AD9834_GPIO_WriteControl(dev);
}

/* 按 Fout*2^28/MCLK 计算频率字，使用整数四舍五入并限制到 28 位。 */
uint32_t AD9834_GPIO_CalcFrequencyWord(uint32_t frequency_hz,
                                       uint32_t mclk_hz)
{
    uint64_t word;

    if (mclk_hz == 0U)
    {
        return 0U;
    }

    word = (((uint64_t)frequency_hz << 28U) + (mclk_hz / 2U)) / mclk_hz;
    if (word > AD9834_FREQ_MASK)
    {
        word = AD9834_FREQ_MASK;
    }

    return (uint32_t)word;
}

static uint32_t AD9834_GPIO_CalcFrequencyWordMilliHz(
    uint32_t frequency_millihz,
    uint32_t mclk_hz)
{
    uint64_t word;
    uint64_t denominator;

    if (mclk_hz == 0U)
    {
        return 0U;
    }

    denominator = (uint64_t)mclk_hz * 1000U;
    word = (((uint64_t)frequency_millihz << 28U) + (denominator / 2U)) /
           denominator;
    if (word > AD9834_FREQ_MASK)
    {
        word = AD9834_FREQ_MASK;
    }

    return (uint32_t)word;
}

static HAL_StatusTypeDef AD9834_GPIO_WriteControl(AD9834_GPIO_HandleTypeDef *dev)
{
    return AD9834_GPIO_WriteRegister(dev, dev->control);
}

static HAL_StatusTypeDef AD9834_GPIO_InitOnePin(GPIO_TypeDef *port, uint16_t pin)
{
    GPIO_InitTypeDef gpio_init_struct;

    if (AD9834_GPIO_EnablePortClock(port) != HAL_OK)
    {
        return HAL_ERROR;
    }

    gpio_init_struct.Pin = pin;
    gpio_init_struct.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init_struct.Pull = GPIO_PULLUP;
    gpio_init_struct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(port, &gpio_init_struct);

    return HAL_OK;
}

static HAL_StatusTypeDef AD9834_GPIO_EnablePortClock(GPIO_TypeDef *port)
{
#ifdef GPIOA
    if (port == GPIOA)
    {
        __HAL_RCC_GPIOA_CLK_ENABLE();
        return HAL_OK;
    }
#endif
#ifdef GPIOB
    if (port == GPIOB)
    {
        __HAL_RCC_GPIOB_CLK_ENABLE();
        return HAL_OK;
    }
#endif
#ifdef GPIOC
    if (port == GPIOC)
    {
        __HAL_RCC_GPIOC_CLK_ENABLE();
        return HAL_OK;
    }
#endif
#ifdef GPIOD
    if (port == GPIOD)
    {
        __HAL_RCC_GPIOD_CLK_ENABLE();
        return HAL_OK;
    }
#endif
#ifdef GPIOE
    if (port == GPIOE)
    {
        __HAL_RCC_GPIOE_CLK_ENABLE();
        return HAL_OK;
    }
#endif
#ifdef GPIOF
    if (port == GPIOF)
    {
        __HAL_RCC_GPIOF_CLK_ENABLE();
        return HAL_OK;
    }
#endif
#ifdef GPIOG
    if (port == GPIOG)
    {
        __HAL_RCC_GPIOG_CLK_ENABLE();
        return HAL_OK;
    }
#endif
#ifdef GPIOH
    if (port == GPIOH)
    {
        __HAL_RCC_GPIOH_CLK_ENABLE();
        return HAL_OK;
    }
#endif
#ifdef GPIOI
    if (port == GPIOI)
    {
        __HAL_RCC_GPIOI_CLK_ENABLE();
        return HAL_OK;
    }
#endif
#ifdef GPIOJ
    if (port == GPIOJ)
    {
        __HAL_RCC_GPIOJ_CLK_ENABLE();
        return HAL_OK;
    }
#endif
#ifdef GPIOK
    if (port == GPIOK)
    {
        __HAL_RCC_GPIOK_CLK_ENABLE();
        return HAL_OK;
    }
#endif

    return HAL_ERROR;
}

static uint8_t AD9834_GPIO_IsValidHandle(const AD9834_GPIO_HandleTypeDef *dev)
{
    return (uint8_t)((dev != 0) &&
                     (dev->sda_port != 0) &&
                     (dev->clk_port != 0) &&
                     (dev->sync_port != 0) &&
                     (dev->reset_port != 0) &&
                     (dev->sda_pin != 0U) &&
                     (dev->clk_pin != 0U) &&
                     (dev->sync_pin != 0U) &&
                     (dev->reset_pin != 0U) &&
                     (dev->mclk_hz != 0U));
}

static void AD9834_GPIO_Delay(const AD9834_GPIO_HandleTypeDef *dev)
{
    volatile uint32_t i;

    for (i = dev->bit_delay_cycles; i > 0U; i--)
    {
        __NOP();
    }
}
