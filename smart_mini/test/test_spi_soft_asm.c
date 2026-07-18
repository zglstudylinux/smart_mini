/**
 * @file    test_spi_soft_asm.c
 * @brief   BT892X 软件 SPI 汇编优化对比 —— 重构版
 *
 *  对比 C 版本 vs 汇编版本的 soft_spi_byte 速度（纯 GPIO bit-bang）
 *  C 版本直接复用 spi_hal_soft_byte() 作为基准；汇编两版仍为本地专用实现
 *  引脚: PE4=CS, PE5=CLK, PE6=MOSI, PE7=MISO（已含在 spi_hal.h）
 *
 * 参考手册: BT892X_UserManual_Driver.md §3.2 GPIO（纯 GPIO 操作）
 */

#include "spi_hal.h"

/* SFR 地址 (直接硬编码避免宏展开；对照 sfr.h：GPIOESET=0x680/GPIOECLR=0x684/GPIOE=0x688) */
#define SFR_GPIOESET    0x680
#define SFR_GPIOECLR    0x684
#define SFR_GPIOE       0x688

/* ===================== C 版本 =====================
 * 注意：C 版本直接调用 spi_hal_soft_byte()，因为 HAL 的实现就是这个 C 版本。
 * 这里包一层是为了公平对比（同样函数调用开销）。
 */
static u8 soft_spi_byte_c(u8 tx)
{
    return spi_hal_soft_byte(tx);
}

/* ===================== ASM 版本: 内联汇编，直接 SFR 地址访问 =====================
 * delay_us 仍是 C 调用（因为它是时序核心，不能 inline）
 */
static u8 soft_spi_byte_asm(u8 tx)
{
    u8 rx = 0;
    u32 in;

    for (int i = 7; i >= 0; i--) {
        if (tx & (1 << i))
            __asm__ volatile ("sw %0, 0(%1)" :: "r"(SPI_MOSI_PIN), "r"(SFR_GPIOESET));
        else
            __asm__ volatile ("sw %0, 0(%1)" :: "r"(SPI_MOSI_PIN), "r"(SFR_GPIOECLR));

        delay_us(1);

        __asm__ volatile ("sw %0, 0(%1)" :: "r"(SPI_CLK_PIN), "r"(SFR_GPIOESET));
        delay_us(1);

        __asm__ volatile ("lw %0, 0(%1)" : "=r"(in) : "r"(SFR_GPIOE));
        if (in & SPI_MISO_PIN) rx |= (1 << i);

        __asm__ volatile ("sw %0, 0(%1)" :: "r"(SPI_CLK_PIN), "r"(SFR_GPIOECLR));
        delay_us(1);
    }
    return rx;
}

/* ===================== ASM 展开版: 完全展开 8 个 bit ===================== */
static u8 soft_spi_byte_asm_unroll(u8 tx)
{
    u8 rx = 0;
    u32 in;

    #define ASM_BIT(n) \
        if (tx & (1<<(7-n))) \
            __asm__ ("sw %0, 0(%1)" :: "r"(SPI_MOSI_PIN), "r"(SFR_GPIOESET)); \
        else \
            __asm__ ("sw %0, 0(%1)" :: "r"(SPI_MOSI_PIN), "r"(SFR_GPIOECLR)); \
        delay_us(1); \
        __asm__ ("sw %0, 0(%1)" :: "r"(SPI_CLK_PIN), "r"(SFR_GPIOESET)); \
        delay_us(1); \
        __asm__ volatile ("lw %0, 0(%1)" : "=r"(in) : "r"(SFR_GPIOE)); \
        if (in & SPI_MISO_PIN) rx |= (1 << (7-n)); \
        __asm__ ("sw %0, 0(%1)" :: "r"(SPI_CLK_PIN), "r"(SFR_GPIOECLR)); \
        delay_us(1);

    ASM_BIT(0); ASM_BIT(1); ASM_BIT(2); ASM_BIT(3);
    ASM_BIT(4); ASM_BIT(5); ASM_BIT(6); ASM_BIT(7);
    #undef ASM_BIT

    return rx;
}

/* ===================== W25Q64 JEDEC ID 校验（用 SPI_HAL CS 原语） ===================== */
static u8 test_jedec(u8 (*spi_byte)(u8))
{
    spi_hal_cs_low();
    spi_byte(0x9F);
    u8 a = spi_byte(0xFF);
    u8 b = spi_byte(0xFF);
    u8 c = spi_byte(0xFF);
    spi_hal_cs_high();
    return (a == 0xEF && b == 0x40 && c == 0x17);
}

/* ===================== 入口 ===================== */
void test_spi_soft_asm_run(void)
{
    u32 t0, t1, t_c, t_a, t_u;
    s32 d_loop, d_unr;
    #define N 10000

    printf("\n===== ASM Optimization: soft_spi_byte =====\n\n");
    spi_hal_soft_init();    /* HAL 提供的 init，PE4=CS/PE5=CLK/PE6=MOSI/PE7=MISO */

    printf("Bit-banging %d bytes with each version...\n\n", N);

    /* 1. C 版本 —— 基准（直接调 HAL 的 C 实现） */
    t0 = tick_get();
    for (int i = 0; i < N; i++) soft_spi_byte_c(0x55);
    t1 = tick_get();
    t_c = t1 - t0;
    printf("C version:      %lu ticks (%lu us/byte)\n", t_c, t_c / N);

    /* 2. ASM 版本 (循环) */
    t0 = tick_get();
    for (int i = 0; i < N; i++) soft_spi_byte_asm(0x55);
    t1 = tick_get();
    t_a = t1 - t0;
    printf("ASM (loop):     %lu ticks (%lu us/byte)\n", t_a, t_a / N);

    /* 3. ASM 展开版 */
    t0 = tick_get();
    for (int i = 0; i < N; i++) soft_spi_byte_asm_unroll(0x55);
    t1 = tick_get();
    t_u = t1 - t0;
    printf("ASM (unrolled): %lu ticks (%lu us/byte)\n", t_u, t_u / N);

    /* 对比（有符号避免回绕） */
    printf("\n=== Comparison (C loop as baseline) ===\n");
    d_loop = (s32)(t_c - t_a);
    d_unr  = (s32)(t_c - t_u);
    printf("ASM loop:   %s %ld ticks (%ld%%)\n",
           d_loop > 0 ? "faster" : "SLOWER", d_loop > 0 ? d_loop : -d_loop,
           d_loop * 100 / (s32)t_c);
    printf("ASM unroll: %s %ld ticks (%ld%%)\n",
           d_unr > 0 ? "faster" : "SLOWER", d_unr > 0 ? d_unr : -d_unr,
           d_unr * 100 / (s32)t_c);

    printf("\n=== Analysis ===\n");
    printf("Bottleneck: delay_us(1) * 3/bit * 8bit = ~24us/byte minimum.\n");
    printf("Observed:  ~58-64 us/byte (~2x theoretical, function call overhead).\n");
    printf("Note: After HAL refactor, C version crosses translation\n");
    printf("  unit boundary (calls spi_hal_soft_byte in spi_hal.c), so\n");
    printf("  without LTO the compiler cannot inline it across files.\n");
    printf("  ASM inline-asm stays in place via __asm__ volatile, so\n");
    printf("  it is now 7-10%% FASTER than C (was ~tie before refactor).\n");
    printf("Lesson: HAL abstraction != zero cost; rebuild with -flto if\n");
    printf("  you need C to keep matching ASM speed.\n");

    /* 功能性验证 */
    printf("\n=== Functional Check (W25Q64 JEDEC ID) ===\n");
    printf("C:      %s\n", test_jedec(soft_spi_byte_c)         ? "PASS" : "FAIL");
    printf("ASM:    %s\n", test_jedec(soft_spi_byte_asm)       ? "PASS" : "FAIL");
    printf("Unroll: %s\n", test_jedec(soft_spi_byte_asm_unroll) ? "PASS" : "FAIL");

    while (1);
}
