/**
 * @file    gpio_test.c
 * @brief   BT892X GPIO 输入/输出测试
 * @author  zglstudylinux
 * @date    2026-07-15
 *
 * 测试内容:
 *   1. PE4 作为输出引脚，循环翻转高低电平，万用表验证
 *   2. PF0 作为输入引脚（10K 上拉），跳线连接 PE4→PF0 验证输入读取
 *
 * 参考手册: BT892X_UserManual_Driver.md §3.2 GPIO 管理
 * 引脚手册: bt892x_pinfunction.md
 */

#include "test.h"

/**
 * @brief GPIO 输出/输入综合测试
 *
 * 硬件连接:
 *   - PE4: 输出引脚（接万用表正极，万用表负极接 GND）
 *   - PF0: 输入引脚（10K 上拉使能）
 *   - 跳线: PE4 ↔ PF0（验证输入读取）
 *
 * 测试步骤:
 *   1. 不接跳线，观察 PE4 万用表读数变化和 PF0 上拉默认高电平
 *   2. 接跳线 PE4→PF0，观察 PF0 读取值跟随 PE4 输出
 */
void gpio_test(void)
{
    // ================================================================
    // 1. 配置 PE4 为输出引脚 (参考手册 §3.2)
    //    DIR=0(输出), DE=1(数字IO), FEN=0(GPIO模式), DRV=1(32mA)
    // ================================================================
    GPIOEFEN &= ~BIT(4);        // bit4=0: GPIO 模式（必须先关功能映射）
    GPIOEDE  |=  BIT(4);        // bit4=1: 数字 IO 使能
    GPIOEDIR &= ~BIT(4);        // bit4=0: 输出模式
    GPIOEDRV |=  BIT(4);        // bit4=1: 32mA 驱动（增强驱动能力）
    GPIOECLR  =  BIT(4);        // 默认输出低电平

    // ================================================================
    // 2. 配置 PF0 为输入引脚 — 关键修复！
    //    必须显式清除三类上拉/下拉（10K / 200K / 300Ω）
    //    使用直接赋值替代 RMW，避免读-改-写失效
    // ================================================================
    GPIOFFEN &= ~BIT(0);        // bit0=0: GPIO 模式（关闭功能映射）
    GPIOFDE  |=  BIT(0);        // bit0=1: 数字 IO 使能
    GPIOFDIR |=  BIT(0);        // bit0=1: 输入模式

    // ★ 显式清除所有上拉/下拉 — 防止残留的 300Ω 强上拉干扰
    GPIOFPU     &= ~BIT(0);     // 10K  上拉: 关
    GPIOFPD     &= ~BIT(0);     // 10K  下拉: 关
    GPIOFPU200K &= ~BIT(0);     // 200K 上拉: 关
    GPIOFPD200K &= ~BIT(0);     // 200K 下拉: 关
    GPIOFPU300  &= ~BIT(0);     // 300Ω 上拉: 关 ★ 最关键！
    GPIOFPD300  &= ~BIT(0);     // 300Ω 下拉: 关

    // ================================================================
    // 3. 调试: 打印关键寄存器值，确认配置生效
    // ================================================================
    printf("\n===== BT892X GPIO Test Start =====\n\n");
    printf("--- Register Dump (Port F) ---\n");
    printf("GPIOFDIR   = 0x%08lx  (bit0=%ld, 1=input)\n", GPIOFDIR,   (GPIOFDIR   >> 0) & 1);
    printf("GPIOFDE    = 0x%08lx  (bit0=%ld, 1=digital)\n", GPIOFDE,  (GPIOFDE    >> 0) & 1);
    printf("GPIOFFEN   = 0x%08lx  (bit0=%ld, 0=GPIO)\n", GPIOFFEN,     (GPIOFFEN   >> 0) & 1);
    printf("GPIOFPU     = 0x%08lx  (bit0=%ld)\n", GPIOFPU,     (GPIOFPU     >> 0) & 1);
    printf("GPIOFPU200K = 0x%08lx  (bit0=%ld)\n", GPIOFPU200K, (GPIOFPU200K >> 0) & 1);
    printf("GPIOFPU300  = 0x%08lx  (bit0=%ld)\n", GPIOFPU300,  (GPIOFPU300  >> 0) & 1);
    printf("GPIOFPD     = 0x%08lx  (bit0=%ld)\n", GPIOFPD,     (GPIOFPD     >> 0) & 1);
    printf("GPIOFPD200K = 0x%08lx  (bit0=%ld)\n", GPIOFPD200K, (GPIOFPD200K >> 0) & 1);
    printf("GPIOFPD300  = 0x%08lx  (bit0=%ld)\n", GPIOFPD300,  (GPIOFPD300  >> 0) & 1);
    printf("GPIOEDRV    = 0x%08lx  (bit4=%ld, 1=32mA)\n", GPIOEDRV, (GPIOEDRV >> 4) & 1);
    printf("------------------------------\n\n");
    printf("PE4: OUTPUT (32mA drive) — connect multimeter +\n");
    printf("PF0: INPUT  (NO pull-up)  — jumper to PE4\n");
    printf("GND: multimeter -\n\n");

    // ================================================================
    // 4. 循环测试: 翻转 PE4，读取 PE4 和 PF0 状态
    // ================================================================
    int cycle = 0;
    while (1) {
        cycle++;

        // 输出高电平
        GPIOESET = BIT(4);                      // PE4 = HIGH (~3.3V)
        delay_ms(1000);

        u32 pe4_out = (GPIOE & BIT(4)) ? 1 : 0;
        u32 pf0_in  = (GPIOF & BIT(0)) ? 1 : 0;

        printf("[Cycle %d] PE4=HIGH(%ld)  PF0=%ld  %s\n",
               cycle, pe4_out, pf0_in,
               (pf0_in == 1) ? "(HIGH)" : "(LOW - follows PE4!)");

        // 输出低电平
        GPIOECLR = BIT(4);                      // PE4 = LOW (~0V)
        delay_ms(1000);

        pe4_out = (GPIOE & BIT(4)) ? 1 : 0;
        pf0_in  = (GPIOF & BIT(0)) ? 1 : 0;

        printf("[Cycle %d] PE4=LOW (%ld)  PF0=%ld  %s\n",
               cycle, pe4_out, pf0_in,
               (pf0_in == 0) ? "(LOW - follows PE4!)" : "(HIGH - PE4 can't pull PF0 down, check wiring)");
    }
}
