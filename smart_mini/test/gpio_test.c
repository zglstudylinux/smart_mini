/**
 * @file    gpio_test.c
 * @brief   BT892X GPIO 输入/输出测试 (v5 - PORTE 方案)
 *
 * PE4 输出(已验证), PE5 输入 — 都在 PORTE，避开 PORTA 的 UART 冲突
 * 参照官方示例写法
 */

#include "test.h"

void gpio_test(void)
{
    printf("\n===== BT892X GPIO Test v5 (PORTE) =====\n\n");

    // ================================================================
    // PE4 → 输出 (已验证可用)
    // ================================================================
    GPIOEFEN &= ~BIT(4);
    GPIOEDE  |=  BIT(4);
    GPIOEDIR &= ~BIT(4);        // 输出

    // ================================================================
    // PE5 → 输入 + 10K 上拉 (参照官方示例，从始至终只做输入)
    // ================================================================
    GPIOEFEN &= ~BIT(5);
    GPIOEDE  |=  BIT(5);
    GPIOEDIR |=  BIT(5);        // 输入
    GPIOEPU  |=  BIT(5);        // 10K 上拉

    // 打印寄存器确认
    printf("GPIOEDIR = 0x%08lx (bit4=%ld out, bit5=%ld in)\n",
           GPIOEDIR, (GPIOEDIR>>4)&1, (GPIOEDIR>>5)&1);
    printf("GPIOEFEN = 0x%08lx (bit4=%ld, bit5=%ld, 0=GPIO)\n",
           GPIOEFEN, (GPIOEFEN>>4)&1, (GPIOEFEN>>5)&1);
    printf("GPIOEPU  = 0x%08lx (bit5=%ld)\n", GPIOEPU, (GPIOEPU>>5)&1);
    printf("\nPE4: OUTPUT   PE5: INPUT(10K PU)\n");
    printf("Jumper: PE4 <-> PE5\n\n");

    // ================================================================
    // 测试循环
    // ================================================================
    int cycle = 0;
    while (1) {
        cycle++;

        GPIOESET = BIT(4);      // PE4 HIGH
        delay_ms(1000);
        printf("[%d] PE4=HIGH  PE5=%ld\n", cycle, (GPIOE & BIT(5)) ? 1 : 0);

        GPIOECLR = BIT(4);      // PE4 LOW
        delay_ms(1000);
        printf("[%d] PE4=LOW   PE5=%ld\n", cycle, (GPIOE & BIT(5)) ? 1 : 0);
    }
}
