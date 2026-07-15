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
    //    DIR=0(输出), DE=1(数字IO), FEN=0(GPIO模式)
    // ================================================================
    GPIOEDIR &= ~BIT(4);        // bit4=0: 输出模式 (0=输出, 1=输入)
    GPIOEDE  |=  BIT(4);        // bit4=1: 数字 IO 使能
    GPIOEFEN &= ~BIT(4);        // bit4=0: GPIO 模式（非功能 IO）

    // 默认输出低电平
    GPIOECLR = BIT(4);

    // ================================================================
    // 2. 配置 PF0 为输入引脚（带 10K 上拉）
    //    DIR=1(输入), DE=1(数字IO), FEN=0(GPIO模式), PU=1(10K上拉)
    // ================================================================
    GPIOFDIR |=  BIT(0);        // bit0=1: 输入模式
    GPIOFDE  |=  BIT(0);        // bit0=1: 数字 IO 使能
    GPIOFFEN &= ~BIT(0);        // bit0=0: GPIO 模式
    GPIOFPU  |=  BIT(0);        // bit0=1: 10K 上拉使能
    GPIOFPD  &= ~BIT(0);        // bit0=0: 下拉关闭

    printf("\n===== BT892X GPIO Test Start =====\n\n");
    printf("PE4: OUTPUT  (connect multimeter + probe)\n");
    printf("PF0: INPUT   (10K pull-up enabled)\n");
    printf("Jumper: connect PE4 <-> PF0 for input test\n\n");

    // ================================================================
    // 3. 循环测试: 每 2 秒翻转 PE4，读取并打印 PE4 和 PF0 状态
    // ================================================================
    int cycle = 0;
    while (1) {
        cycle++;

        // 输出高电平
        GPIOESET = BIT(4);                      // PE4 = HIGH (~3.3V)
        delay_ms(1000);                         // 持续 1 秒

        // 读取引脚状态
        u32 pe4_out = (GPIOE >> 4) & 1;        // 读 PE4 输出状态
        u32 pf0_in  = (GPIOF >> 0) & 1;        // 读 PF0 输入状态

        printf("[Cycle %d] PE4=HIGH(%ld)  PF0=%ld  %s\n",
               cycle, pe4_out, pf0_in,
               (pf0_in == 1) ? "(PULL-UP OK or JUMPER HIGH)" : "(LOW - check jumper)");

        // 输出低电平
        GPIOECLR = BIT(4);                      // PE4 = LOW (~0V)
        delay_ms(1000);                         // 持续 1 秒

        pe4_out = (GPIOE >> 4) & 1;
        pf0_in  = (GPIOF >> 0) & 1;

        printf("[Cycle %d] PE4=LOW (%ld)  PF0=%ld  %s\n",
               cycle, pe4_out, pf0_in,
               (pf0_in == 0) ? "(JUMPER OK - follows PE4)" : "(NO JUMPER - pull-up keeps HIGH)");
    }
}
