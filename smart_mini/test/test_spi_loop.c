/**
 * @file    test_spi_loop.c
 * @brief   BT892X SPI 跳线回环测试（软件 bit-bang + 硬件 SPI1）
 *
 *  接线：跳线 PE7↔PE5；不接 Flash 不接 LA
 *  编译宏 TEST_SPI_LOOP_PHASE 选择：
 *    0 = 全跑（软回环 + 硬回环，默认）
 *    1 = 只软回环
 *    2 = 只硬回环
 *
 * 参考手册: BT892X_UserManual_Driver.md §3.2 GPIO、§3.3 FUNCMCON1、§7 SPI
 * 引脚手册: bt892x_pinfunction.md §4.3 PORTE、§5.2 SPI1
 */

#include "test_spi_common.h"

#ifndef TEST_SPI_LOOP_PHASE
#define TEST_SPI_LOOP_PHASE 0
#endif

/* ===================== 软件 SPI bit-bang (PE5=CLK, PE6=MOSI, PE7=MISO) ===================== */
static void soft_spi_init(void)
{
    // CS、CLK、MOSI 设为普通 GPIO 输出；MISO 设为输入
    // 与硬件 SPI 共用 PE4..PE7，但角色不同，所以从硬件 SPI 退出后要把 FEN 清掉
    GPIOEFEN &= ~(SPI_CS_PIN | SPI_CLK_PIN | SPI_MOSI_PIN | SPI_MISO_PIN);
    GPIOEDE  |=  (SPI_CS_PIN | SPI_CLK_PIN | SPI_MOSI_PIN | SPI_MISO_PIN);

    // 软件 SPI 引脚映射：PE4=CS, PE5=CLK, PE6=MOSI, PE7=MISO
    GPIOEDIR &= ~(SPI_CS_PIN | SPI_CLK_PIN | SPI_MOSI_PIN); // CS/CLK/MOSI 输出
    GPIOEDIR |=   SPI_MISO_PIN;                              // MISO 输入
    GPIOESET  =   SPI_CS_PIN;                                // CS 空闲高
    GPIOECLR  =   SPI_CLK_PIN;                               // CLK 空闲低（Mode 0）
}

static u8 soft_spi_byte(u8 tx)
{
    u8 rx = 0;
    for (int i = 7; i >= 0; i--) {
        if (tx & (1 << i)) GPIOESET = SPI_MOSI_PIN;  // §3.2: MOSI=1
        else               GPIOECLR = SPI_MOSI_PIN;  // §3.2: MOSI=0
        delay_us(1);
        GPIOESET = SPI_CLK_PIN; delay_us(1);          // §3.2: CLK 上升沿
        if (GPIOE & SPI_MISO_PIN) rx |= (1 << i);     // §3.2: 中心采样 MISO
        GPIOECLR = SPI_CLK_PIN; delay_us(1);          // §3.2: CLK 下降沿
    }
    return rx;
}

/* ===================== 硬件 SPI1 (PE6=CLK, PE7=MOSI, PE5=MISO) ===================== */
static void hw_spi_init(u32 baud)
{
    // 引脚映射 G4 (FUNCMCON1[15:12]=0x4)
    FUNCMCON1 &= ~(0xF << 12);
    FUNCMCON1 |=  (0x4 << 12);

    // CLK/MOSI/MISO → 功能 IO；CS 用 GPIO 手动控制
    GPIOEFEN |= SPI_CLK_PIN | SPI_MOSI_PIN | SPI_MISO_PIN;
    GPIOEDE  |= SPI_CLK_PIN | SPI_MOSI_PIN | SPI_MISO_PIN;
    GPIOEDIR &= ~(SPI_CLK_PIN | SPI_MOSI_PIN);   // CLK/MOSI 输出
    GPIOEDIR |=   SPI_MISO_PIN;                   // MISO 输入

    GPIOEFEN &= ~SPI_CS_PIN;                     // CS 用普通 GPIO
    GPIOEDE  |=  SPI_CS_PIN;
    GPIOEDIR &= ~SPI_CS_PIN;
    GPIOESET  =  SPI_CS_PIN;                     // CS 空闲高

    SPI1BAUD = baud;       // 100KHz = 239
    SPI1CON  = BIT(0);     // SPIEN (Mode 0)
}

static u8 hw_spi_byte(u8 tx)
{
    SPI1BUF = tx;
    while (!(SPI1CON & BIT(16)));
    SPI1CPND = BIT(16);
    return (u8)SPI1BUF;
}

/* ===================== 子阶段 ===================== */
static int run_soft_loopback(void)
{
    printf("\n##### Phase 1: Software SPI Loopback (bit-bang) #####\n");
    printf("PE4=CS, PE5=CLK, PE6=MOSI, PE7=MISO\n");
    printf("Wiring: jumper PE7 <-> PE5\n\n");
    soft_spi_init();

    int err = 0;
    for (int v = 0; v <= 0xFF; v++) {
        u8 rx = soft_spi_byte((u8)v);
        if (rx != (u8)v) { err++; if (err <= 3) TEST_LOG("ERR: s=0x%02X r=0x%02X", v, rx); }
    }
    printf("Soft Loopback: %s (%d/256 errors)\n\n", err ? "FAILED" : "PASSED", err);
    return err;
}

static int run_hw_loopback(void)
{
    printf("\n##### Phase 2: Hardware SPI1 Loopback #####\n");
    printf("PE4=CS(GPIO), PE6=CLK, PE7=MOSI, PE5=MISO (G4)\n");
    printf("Wiring: jumper PE7 <-> PE5\n\n");
    hw_spi_init(239);   // 100KHz

    int err = 0;
    for (int v = 0; v <= 0xFF; v++) {
        u8 rx = hw_spi_byte((u8)v);
        if (rx != (u8)v) { err++; if (err <= 3) TEST_LOG("ERR: s=0x%02X r=0x%02X", v, rx); }
    }
    printf("HW Loopback: %s (%d/256 errors)\n\n", err ? "FAILED" : "PASSED", err);
    return err;
}

/* ===================== 入口 ===================== */
void test_spi_loop_run(void)
{
    printf("\n===== BT892X SPI Loopback Test (Software + Hardware) =====\n");
    printf("Wiring: jumper PE7 <-> PE5\n");
    printf("(NOT with W25Q64 Flash, NOT with Logic Analyzer)\n\n");

    int total_err = 0;

#if TEST_SPI_LOOP_PHASE == 1
    total_err += run_soft_loopback();
#elif TEST_SPI_LOOP_PHASE == 2
    total_err += run_hw_loopback();
#else
    // Phase 0: 全跑
    total_err += run_soft_loopback();
    total_err += run_hw_loopback();
#endif

    printf("===== SPI Loopback Test DONE =====\n");
    printf("Summary: %s (total errors=%d)\n",
           total_err ? "FAILED" : "ALL PASSED", total_err);
    while (1);
}
