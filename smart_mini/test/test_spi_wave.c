/**
 * @file    test_spi_wave.c
 * @brief   BT892X SPI 逻辑分析仪波形测试（软件 bit-bang + 硬件 SPI1）
 *
 *  接线：LA 接 PE6(CLK) + PE7(MOSI)；不接跳线不接 Flash
 *  编译宏 TEST_SPI_WAVE_PHASE 选择：
 *    0 = 全跑（软波形 + 硬波形，默认）
 *    1 = 只软波形
 *    2 = 只硬波形
 *
 *  每个相位：发 0x55/AA/00/FF/F0/0F 6 个字节 + 持续 0x55（供 LA 抓取）
 *  软件相位：测试模式软件 SPI 通过同一引脚输出
 *  硬件相位：硬件 SPI1 G4 输出
 *
 * 参考手册: BT892X_UserManual_Driver.md §3.2 GPIO、§3.3 FUNCMCON1、§7 SPI
 */

#include "test_spi_common.h"

#ifndef TEST_SPI_WAVE_PHASE
#define TEST_SPI_WAVE_PHASE 2
#endif

// LA 配置: SCLK=CH0 接 PE6, MOSI=CH1 接 PE7，CPOL=0/CPHA=0, MSB-first
// 软/硬 SPI 都发单一字节 0x55 持续循环，方便看 clk+mosi 波形
#define WAVE_BYTE   0x55

/* ===================== 软件 SPI bit-bang ===================== */
// 引脚与硬件 SPI1(G4) 对齐：CS=PE4, CLK=PE6, MOSI=PE7, MISO=PE5
static void soft_spi_init(void)
{
    GPIOEFEN &= ~(SPI_CS_PIN | SPI_CLK_PIN | SPI_MOSI_PIN | SPI_MISO_PIN);
    GPIOEDE  |=  (SPI_CS_PIN | SPI_CLK_PIN | SPI_MOSI_PIN | SPI_MISO_PIN);
    GPIOEDIR &= ~(SPI_CS_PIN | SPI_CLK_PIN | SPI_MOSI_PIN);
    GPIOEDIR |=   SPI_MISO_PIN;
    GPIOESET  =   SPI_CS_PIN;
    GPIOECLR  =   SPI_CLK_PIN;
}

static void soft_spi_byte(u8 tx)
{
    for (int i = 7; i >= 0; i--) {
        if (tx & (1 << i)) GPIOESET = SPI_MOSI_PIN;
        else               GPIOECLR = SPI_MOSI_PIN;
        delay_us(1);
        GPIOESET = SPI_CLK_PIN; delay_us(1);
        GPIOECLR = SPI_CLK_PIN; delay_us(1);
    }
}

/* ===================== 硬件 SPI1 ===================== */
static void hw_spi_init(u32 baud)
{
    FUNCMCON1 &= ~(0xF << 12);
    FUNCMCON1 |=  (0x4 << 12);
    GPIOEFEN |= SPI_CLK_PIN | SPI_MOSI_PIN | SPI_MISO_PIN;
    GPIOEDE  |= SPI_CLK_PIN | SPI_MOSI_PIN | SPI_MISO_PIN;
    GPIOEDIR &= ~(SPI_CLK_PIN | SPI_MOSI_PIN);
    GPIOEDIR |=   SPI_MISO_PIN;
    GPIOEFEN &= ~SPI_CS_PIN;
    GPIOEDE  |=  SPI_CS_PIN;
    GPIOEDIR &= ~SPI_CS_PIN;
    GPIOESET  =  SPI_CS_PIN;
    SPI1BAUD = baud;
    SPI1CON  = BIT(0);
}

static void hw_spi_byte(u8 tx)
{
    SPI1BUF = tx;
    while (!(SPI1CON & BIT(16)));
    SPI1CPND = BIT(16);
}

/* ===================== 子阶段 ===================== */
static void run_soft_waveform(void)
{
    printf("\n##### Phase 1: Software SPI Waveform #####\n");
    printf("CS=PE4, CLK=PE6, MOSI=PE7（软件SPI与硬件G4同引脚）\n");
    printf("LA: CH0=PE6(CLK), CH1=PE7(MOSI)\n");
    printf("Sending 0x55 continuously on PE7...\n\n");
    soft_spi_init();

    // 持续发 0x55 直到 LA 抓完（Mode 0, MSB-first）
    while (1) {
        GPIOECLR = SPI_CS_PIN; delay_us(1);    // CS LOW
        soft_spi_byte(WAVE_BYTE);
        GPIOESET = SPI_CS_PIN; delay_us(1);    // CS HIGH
        delay_us(100);
    }
}

static void run_hw_waveform(void)
{
    printf("\n##### Phase 2: Hardware SPI1 Waveform #####\n");
    printf("CS=PE4(GPIO), CLK=PE6, MOSI=PE7（硬件SPI1 G4）\n");
    printf("LA: CH0=PE6(CLK), CH1=PE7(MOSI)\n");
    printf("Sending 0x55 continuously on PE7...\n\n");
    hw_spi_init(239);

    // 持续发 0x55 直到 LA 抓完（Mode 0, MSB-first）
    while (1) {
        GPIOECLR = SPI_CS_PIN; delay_us(1);
        hw_spi_byte(WAVE_BYTE);
        GPIOESET = SPI_CS_PIN;
        delay_us(10);
    }
}

/* ===================== 入口 ===================== */
void test_spi_wave_run(void)
{
    printf("\n===== BT892X SPI Waveform Test (Software + Hardware) =====\n");
    printf("LA: CH1 = CLK, CH2 = MOSI\n");
    printf("(NOT with jumper, NOT with W25Q64 Flash)\n\n");

#if TEST_SPI_WAVE_PHASE == 1
    run_soft_waveform();
#elif TEST_SPI_WAVE_PHASE == 2
    run_hw_waveform();
#else
    run_soft_waveform();   // (while(1) — only soft runs in Phase 0)
    // Note: Phase 0 默认跑软然后停在 while(1)，如需全跑需单独编译两次
#endif

    printf("===== SPI Waveform DONE =====\n");
    while (1);
}
