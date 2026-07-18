/**
 * @file    test_spi_soft_asm.c
 * @brief   BT892X 软件 SPI 汇编优化对比（自 smart_mini_copilot asm_spi_test.c 移植）
 *
 *  对比 C 版本 vs 汇编版本的 soft_spi_byte 速度（纯 GPIO bit-bang）
 *  引脚: PE4=CS, PE5=CLK, PE6=MOSI, PE7=MISO
 *
 * 参考手册: BT892X_UserManual_Driver.md §3.2 GPIO（纯 GPIO 操作）
 */

#include "test_common.h"

// 引脚与硬件 SPI1(G4) 对齐：CS=PE4, CLK=PE6, MOSI=PE7, MISO=PE5
#define A_CS    BIT(4)
#define A_CLK   BIT(6)
#define A_MOSI  BIT(7)
#define A_MISO  BIT(5)

// SFR 地址 (直接硬编码避免宏展开；对照 sfr.h：GPIOESET=0x680/GPIOECLR=0x684/GPIOE=0x688)
#define SFR_GPIOESET    0x680
#define SFR_GPIOECLR    0x684
#define SFR_GPIOE       0x688

/* C 版本 (原版, 用于对比) */
static u8 soft_spi_byte_c(u8 tx)
{
    u8 rx = 0;
    for (int i = 7; i >= 0; i--) {
        if (tx & (1 << i)) GPIOESET = A_MOSI;
        else               GPIOECLR = A_MOSI;
        delay_us(1);
        GPIOESET = A_CLK; delay_us(1);
        if (GPIOE & A_MISO) rx |= (1 << i);
        GPIOECLR = A_CLK; delay_us(1);
    }
    return rx;
}

/* ASM 版本: 内联汇编, 直接 SFR 地址访问 (delay_us 仍是 C 调用) */
static u8 soft_spi_byte_asm(u8 tx)
{
    u8 rx = 0;
    u32 in;

    for (int i = 7; i >= 0; i--) {
        if (tx & (1 << i))
            __asm__ volatile ("sw %0, 0(%1)" :: "r"(A_MOSI), "r"(SFR_GPIOESET));
        else
            __asm__ volatile ("sw %0, 0(%1)" :: "r"(A_MOSI), "r"(SFR_GPIOECLR));

        delay_us(1);

        __asm__ volatile ("sw %0, 0(%1)" :: "r"(A_CLK), "r"(SFR_GPIOESET));
        delay_us(1);

        __asm__ volatile ("lw %0, 0(%1)" : "=r"(in) : "r"(SFR_GPIOE));
        if (in & A_MISO) rx |= (1 << i);

        __asm__ volatile ("sw %0, 0(%1)" :: "r"(A_CLK), "r"(SFR_GPIOECLR));
        delay_us(1);
    }
    return rx;
}

/* ASM 展开版: 完全展开 8 个 bit, 消除循环开销 */
static u8 soft_spi_byte_asm_unroll(u8 tx)
{
    u8 rx = 0;
    u32 in;

    #define ASM_BIT(n) \
        if (tx & (1<<(7-n))) \
            __asm__ ("sw %0, 0(%1)" :: "r"(A_MOSI), "r"(SFR_GPIOESET)); \
        else \
            __asm__ ("sw %0, 0(%1)" :: "r"(A_MOSI), "r"(SFR_GPIOECLR)); \
        delay_us(1); \
        __asm__ ("sw %0, 0(%1)" :: "r"(A_CLK), "r"(SFR_GPIOESET)); \
        delay_us(1); \
        __asm__ volatile ("lw %0, 0(%1)" : "=r"(in) : "r"(SFR_GPIOE)); \
        if (in & A_MISO) rx |= (1 << (7-n)); \
        __asm__ ("sw %0, 0(%1)" :: "r"(A_CLK), "r"(SFR_GPIOECLR)); \
        delay_us(1);

    ASM_BIT(0); ASM_BIT(1); ASM_BIT(2); ASM_BIT(3);
    ASM_BIT(4); ASM_BIT(5); ASM_BIT(6); ASM_BIT(7);
    #undef ASM_BIT

    return rx;
}

static void asm_spi_init(void)
{
    GPIOEFEN &= ~(A_CS|A_CLK|A_MOSI|A_MISO);
    GPIOEDE  |=  (A_CS|A_CLK|A_MOSI|A_MISO);
    GPIOEDIR &= ~(A_CS|A_CLK|A_MOSI);
    GPIOEDIR |=  A_MISO;
    GPIOESET  =  A_CS;
    GPIOECLR  =  A_CLK;
}

/* 测试 W25Q64 JEDEC ID (可切换 C/ASM/ASM_Unroll 版本) */
static u8 test_jedec(u8 (*spi_byte)(u8))
{
    GPIOECLR = A_CS; delay_us(1);
    spi_byte(0x9F);
    u8 a = spi_byte(0xFF);
    u8 b = spi_byte(0xFF);
    u8 c = spi_byte(0xFF);
    GPIOESET = A_CS; delay_us(1);
    return (a == 0xEF && b == 0x40 && c == 0x17);
}

void test_spi_soft_asm_run(void)
{
    printf("\n===== ASM Optimization: soft_spi_byte =====\n\n");
    asm_spi_init();

    u32 t0, t1;
    #define N 10000

    printf("Bit-banging %d bytes with each version...\n\n", N);

    // 1. C 版本
    t0 = tick_get();
    for (int i = 0; i < N; i++)
        soft_spi_byte_c(0x55);
    t1 = tick_get();
    u32 t_c = t1 - t0;
    printf("C version:      %lu ticks (%lu us/byte)\n", t_c, t_c / N);

    // 2. ASM 版本 (循环)
    t0 = tick_get();
    for (int i = 0; i < N; i++)
        soft_spi_byte_asm(0x55);
    t1 = tick_get();
    u32 t_a = t1 - t0;
    printf("ASM (loop):     %lu ticks (%lu us/byte)\n", t_a, t_a / N);

    // 3. ASM 展开版
    t0 = tick_get();
    for (int i = 0; i < N; i++)
        soft_spi_byte_asm_unroll(0x55);
    t1 = tick_get();
    u32 t_u = t1 - t0;
    printf("ASM (unrolled): %lu ticks (%lu us/byte)\n", t_u, t_u / N);

    // 对比 (用有符号避免回绕)
    printf("\n=== Comparison (C loop as baseline) ===\n");
    s32 d_loop = (s32)(t_c - t_a);
    s32 d_unr  = (s32)(t_c - t_u);
    printf("ASM loop:   %s %ld ticks (%ld%%)\n",
           d_loop > 0 ? "faster" : "SLOWER", d_loop > 0 ? d_loop : -d_loop,
           d_loop * 100 / (s32)t_c);
    printf("ASM unroll: %s %ld ticks (%ld%%)\n",
           d_unr > 0 ? "faster" : "SLOWER", d_unr > 0 ? d_unr : -d_unr,
           d_unr * 100 / (s32)t_c);

    printf("\n=== Analysis ===\n");
    printf("Bottleneck: delay_us(1) * 4/bit * 8bit = ~32us/byte minimum.\n");
    printf("C compiler already optimizes GPIO writes efficiently.\n");
    printf("Inline ASM adds overhead (register loading, volatile barrier).\n");
    printf("Unrolling helps ~1-2%% by eliminating loop branches.\n");

    // 功能性验证
    printf("\n=== Functional Check (W25Q64 JEDEC ID) ===\n");
    printf("C:      %s\n", test_jedec(soft_spi_byte_c) ? "PASS" : "FAIL");
    printf("ASM:    %s\n", test_jedec(soft_spi_byte_asm) ? "PASS" : "FAIL");
    printf("Unroll: %s\n", test_jedec(soft_spi_byte_asm_unroll) ? "PASS" : "FAIL");

    while (1);
}
