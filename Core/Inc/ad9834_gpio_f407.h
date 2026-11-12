/**
  ******************************************************************************
  * @file    ad9834_gpio_f407.h
  * @brief   AD9834 GPIO 模拟串行接口驱动。
  *
  * 依据资料包中的 AD9834 芯片手册、模块使用手册和 STM32 GPIO 例程整理。
  * 驱动按 MSB first 发送 16 位字，频率控制字为 28 位，相位控制字为 12 位。
  *
  * @note 输出频率公式：Fout = frequency_word * MCLK / 2^28。
  * @warning AD9834 没有数字幅度寄存器。本驱动只能设置频率、相位、波形、
  *          复位和休眠；输出幅度由 FS ADJUST/RSET、负载和外部放大电路决定。
  ******************************************************************************
  */

#ifndef AD9834_GPIO_F407_H
#define AD9834_GPIO_F407_H

#include "stm32f4xx_hal.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 资料包模块板载有源晶振标称 75MHz。更换 MCLK 后必须同步修改。 */
#define AD9834_GPIO_DEFAULT_MCLK_HZ      75000000UL
/** GPIO 翻转之间的默认空循环次数；飞线较长时可适当增大。 */
#define AD9834_GPIO_DEFAULT_BIT_DELAY    10UL

/** 当前 FFT 工程默认接线：PC0/PC1/PC2/PC3。 */
#define AD9834_GPIO_DEFAULT_SDA_PORT     GPIOC
#define AD9834_GPIO_DEFAULT_SDA_PIN      GPIO_PIN_0
#define AD9834_GPIO_DEFAULT_CLK_PORT     GPIOC
#define AD9834_GPIO_DEFAULT_CLK_PIN      GPIO_PIN_1
#define AD9834_GPIO_DEFAULT_SYNC_PORT    GPIOC
#define AD9834_GPIO_DEFAULT_SYNC_PIN     GPIO_PIN_2
#define AD9834_GPIO_DEFAULT_RESET_PORT   GPIOC
#define AD9834_GPIO_DEFAULT_RESET_PIN    GPIO_PIN_3

/** 资料例程中的完整控制字，可供直接调用 WriteRegister() 时使用。 */
#define AD9834_GPIO_REG_SINE_WAVE         0x2008U
#define AD9834_GPIO_REG_TRIANGLE_WAVE     0x2002U
#define AD9834_GPIO_REG_SQUARE_WAVE       0x2028U
#define AD9834_GPIO_REG_SQUARE_DIV2_WAVE  0x2020U

/** AD9834 可选输出波形。正弦和三角波来自 DAC，方波来自 SIGN BIT OUT。 */
typedef enum
{
    AD9834_GPIO_WAVE_SINE = 0,          /**< DAC 正弦波。 */
    AD9834_GPIO_WAVE_TRIANGLE,          /**< DAC 三角波。 */
    AD9834_GPIO_WAVE_SQUARE,            /**< DAC 数据 MSB 方波，不二分频。 */
    AD9834_GPIO_WAVE_SQUARE_DIV2,       /**< DAC 数据 MSB/2 方波。 */
    AD9834_GPIO_WAVE_SQUARE_COMPARATOR  /**< 内部比较器方波输出。 */
} AD9834_GPIO_Waveform;

/** 两个独立 28 位频率寄存器。FSEL 控制当前使用哪一个。 */
typedef enum
{
    AD9834_GPIO_FREQ_REG0 = 0,
    AD9834_GPIO_FREQ_REG1 = 1
} AD9834_GPIO_FreqReg;

/** 两个独立 12 位相位寄存器。PSEL 控制当前使用哪一个。 */
typedef enum
{
    AD9834_GPIO_PHASE_REG0 = 0,
    AD9834_GPIO_PHASE_REG1 = 1
} AD9834_GPIO_PhaseReg;

/** AD9834 实例配置及控制寄存器的软件镜像。 */
typedef struct
{
    GPIO_TypeDef *sda_port;   /**< SDATA 所在 GPIO 端口。 */
    uint16_t sda_pin;         /**< SDATA 引脚。 */
    GPIO_TypeDef *clk_port;   /**< SCLK 所在 GPIO 端口。 */
    uint16_t clk_pin;         /**< SCLK 引脚。 */
    GPIO_TypeDef *sync_port;  /**< FSYNC 所在 GPIO 端口。 */
    uint16_t sync_pin;        /**< FSYNC 引脚。 */
    GPIO_TypeDef *reset_port; /**< 模块硬件 RESET 所在 GPIO 端口。 */
    uint16_t reset_pin;       /**< 模块硬件 RESET 引脚。 */
    uint32_t mclk_hz;         /**< AD9834 实际主时钟，单位 Hz。 */
    uint32_t bit_delay_cycles;/**< GPIO 模拟时序空循环次数。 */
    uint16_t control;         /**< AD9834 控制寄存器的软件镜像。 */
} AD9834_GPIO_HandleTypeDef;

/**
  * @brief 使用 PC0~PC3 和 75MHz MCLK 初始化 AD9834。
  * @param dev 驱动实例，不能为 NULL。
  * @return HAL_OK 表示 GPIO、硬件复位和初始寄存器写入成功。
  * @note 初始化写入 FREQ0=0、PHASE0=0，并默认选择正弦 DAC 路径。
  */
HAL_StatusTypeDef AD9834_GPIO_InitDefault(AD9834_GPIO_HandleTypeDef *dev);

/**
  * @brief 使用自定义 GPIO 和 MCLK 初始化 AD9834。
  * @param dev 驱动实例。
  * @param sda_port SDATA 端口。
  * @param sda_pin SDATA 引脚。
  * @param clk_port SCLK 端口。
  * @param clk_pin SCLK 引脚。
  * @param sync_port FSYNC 端口。
  * @param sync_pin FSYNC 引脚。
  * @param reset_port 硬件 RESET 端口。
  * @param reset_pin 硬件 RESET 引脚。
  * @param mclk_hz AD9834 实际主时钟；传 0 时使用默认 75MHz。
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
                                   uint32_t mclk_hz);

/**
  * @brief 设置 GPIO 模拟串行接口每个边沿之间的空循环次数。
  * @note 数值越大通信越慢；它不是精确的纳秒或微秒延时。
  */
void AD9834_GPIO_SetBitDelayCycles(AD9834_GPIO_HandleTypeDef *dev,
                                   uint32_t cycles);

/**
  * @brief 向 AD9834 发送一个原始 16 位寄存器字。
  * @note 函数自动控制 FSYNC，按最高位先发，SCLK 空闲为高。
  */
HAL_StatusTypeDef AD9834_GPIO_WriteRegister(AD9834_GPIO_HandleTypeDef *dev,
                                            uint16_t value);

/**
  * @brief 在软件 RESET 期间配置 FREQ0 和波形，再解除 RESET 输出。
  * @param frequency_hz 目标输出频率，范围 0~MCLK/2。
  * @param waveform 正弦、三角或方波模式。
  * @note 本函数不改变相位寄存器，也不能改变输出幅度。
  */
HAL_StatusTypeDef AD9834_GPIO_SetOutput(AD9834_GPIO_HandleTypeDef *dev,
                                        uint32_t frequency_hz,
                                        AD9834_GPIO_Waveform waveform);

/** Set FREQ0 in millihertz while preserving the existing Hz API. */
HAL_StatusTypeDef AD9834_GPIO_SetOutputMilliHz(
    AD9834_GPIO_HandleTypeDef *dev,
    uint32_t frequency_millihz,
    AD9834_GPIO_Waveform waveform);

/** @brief 按 Hz 更新并选择 FREQ0，不主动进入软件 RESET。 */
HAL_StatusTypeDef AD9834_GPIO_SetFrequencyHz(AD9834_GPIO_HandleTypeDef *dev,
                                             uint32_t frequency_hz);

/** Update active FREQ0 in millihertz without resetting the phase accumulator. */
HAL_StatusTypeDef AD9834_GPIO_SetFrequencyMilliHz(
    AD9834_GPIO_HandleTypeDef *dev,
    uint32_t frequency_millihz);

/**
  * @brief 把 Hz 换算为 28 位控制字并写入指定频率寄存器。
  * @note 只写寄存器，不改变当前 FSEL 选择。
  */
HAL_StatusTypeDef AD9834_GPIO_WriteFrequencyHz(AD9834_GPIO_HandleTypeDef *dev,
                                               AD9834_GPIO_FreqReg reg,
                                               uint32_t frequency_hz);

/**
  * @brief 把原始 28 位频率字拆成低/高两个 14 位字写入 FREQ0 或 FREQ1。
  * @note 函数确保 B28=1，写入顺序为低 14 位后高 14 位。
  */
HAL_StatusTypeDef AD9834_GPIO_WriteFrequencyWord(AD9834_GPIO_HandleTypeDef *dev,
                                                 AD9834_GPIO_FreqReg reg,
                                                 uint32_t frequency_word);

/** @brief 修改控制寄存器 FSEL 位，选择 FREQ0 或 FREQ1 作为输出频率。 */
HAL_StatusTypeDef AD9834_GPIO_SelectFrequencyRegister(
    AD9834_GPIO_HandleTypeDef *dev,
    AD9834_GPIO_FreqReg reg);

/**
  * @brief 向 PHASE0 或 PHASE1 写入原始 12 位相位字。
  * @note 相位分辨率为 360/4096 度；写入不会自动选择该寄存器。
  */
HAL_StatusTypeDef AD9834_GPIO_WritePhaseWord(AD9834_GPIO_HandleTypeDef *dev,
                                             AD9834_GPIO_PhaseReg reg,
                                             uint16_t phase_word);

/**
  * @brief 把角度换算为 12 位相位字后写入指定相位寄存器。
  * @param degree 任意无符号角度，函数内部按 360 度取模。
  */
HAL_StatusTypeDef AD9834_GPIO_WritePhaseDegree(AD9834_GPIO_HandleTypeDef *dev,
                                               AD9834_GPIO_PhaseReg reg,
                                               uint16_t degree);

/** @brief 修改控制寄存器 PSEL 位，选择 PHASE0 或 PHASE1。 */
HAL_StatusTypeDef AD9834_GPIO_SelectPhaseRegister(
    AD9834_GPIO_HandleTypeDef *dev,
    AD9834_GPIO_PhaseReg reg);

/**
  * @brief 配置 MODE、OPBITEN、SIGN/PIB 和 DIV2 位以选择输出波形。
  * @note 正弦/三角波使用 DAC 输出；方波应从 SIGN BIT OUT 获取。
  */
HAL_StatusTypeDef AD9834_GPIO_SetWaveform(AD9834_GPIO_HandleTypeDef *dev,
                                          AD9834_GPIO_Waveform waveform);

/**
  * @brief 设置控制寄存器 RESET 位，暂停或恢复 DDS 数字输出。
  * @note 这是软件 RESET，不是句柄中 PC3 对应的模块硬件 RESET 引脚。
  */
HAL_StatusTypeDef AD9834_GPIO_SetSoftwareReset(AD9834_GPIO_HandleTypeDef *dev,
                                               uint8_t enable);

/**
  * @brief 独立控制 DAC 和内部 MCLK 的低功耗休眠位。
  * @param sleep_dac 非 0 时置 SLEEP12，使 DAC 休眠。
  * @param sleep_mclk 非 0 时置 SLEEP1，使内部 MCLK 休眠。
  */
HAL_StatusTypeDef AD9834_GPIO_SetSleep(AD9834_GPIO_HandleTypeDef *dev,
                                       uint8_t sleep_dac,
                                       uint8_t sleep_mclk);

/**
  * @brief 根据 Fout*2^28/MCLK 计算并四舍五入 28 位频率控制字。
  * @return mclk_hz 为 0 时返回 0；超出 28 位时钳位到最大值。
  */
uint32_t AD9834_GPIO_CalcFrequencyWord(uint32_t frequency_hz,
                                       uint32_t mclk_hz);

#ifdef __cplusplus
}
#endif

#endif /* AD9834_GPIO_F407_H */
