/**
 * @file    timer_pwm_test.c
 * @brief   BT892X Timer3 PWM 输出测试
 *
 * 测试内容:
 *   Timer3 三路 PWM 同时输出，逻辑分析仪验证周期和占空比
 *   - PWM0 → PB0: 25% 占空比
 *   - PWM1 → PB1: 50% 占空比
 *   - PWM2 → PB2: 75% 占空比
 *
 * 参考手册: BT892X_UserManual_Driver.md §4 定时器
 * 引脚手册: bt892x_pinfunction.md
 *
 * 关键公式 (手册 §4.3):
 *   PWM 周期 = TMR3PR + 1 (个时钟周期)
 *   低电平长度 = DUTY + 1
 *   高电平长度 = PR - DUTY
 *   占空比(高) = (PR - DUTY) / (PR + 1)
 */

#include "test.h"

void timer_pwm_test(void)
{
    printf("\n===== BT892X Timer3 PWM Test =====\n\n");

    // ================================================================
    // 1. 配置 PWM 引脚映射: TMR3MAP = G1 (FUNCMCON2[11:8] = 0x1)
    //    PWM0 → PB0, PWM1 → PB1, PWM2 → PB2
    //
    //    ★ 必须先清除冲突映射:
    //       - SD0MAP=0xF: main.c 中 uart0_mapping_sel() 的 = 赋值把它清零了
    //         如果 SD0MAP=0 可能默认占用 PB0(SDCMD-G2)
    //       - TMR3CPTMAP=0xF: 防止 PB0 被误映射为捕获输入(TMR3CAP_G3)
    // ================================================================
    FUNCMCON0 |= 0xF;                           // SD0MAP = 0xF (clear, 释放 PB0)
    FUNCMCON2 &= ~((0xF << 4) | (0xF << 8));    // 清除 TMR3CPTMAP + TMR3MAP
    FUNCMCON2 |= (0xF << 4) | (0x1 << 8);       // TMR3CPTMAP=clear, TMR3MAP=G1

    // PB0/PB1/PB2 → 功能 IO 模式 (PWM 输出)
    GPIOBFEN |= BIT(0) | BIT(1) | BIT(2);   // 功能 IO 模式
    GPIOBDE  |= BIT(0) | BIT(1) | BIT(2);   // 数字 IO
    GPIOBDIR &= ~(BIT(0) | BIT(1) | BIT(2)); // 输出方向

    // ================================================================
    // 2. 配置 Timer3 参数
    //    时钟源: tmr_inc = 1MHz (与 main.c 一致)
    //    PWM 频率: 1KHz (周期 = 1000 个 tick)
    // ================================================================
    u32 pr_val = 1000 - 1;          // PR = 999, 周期 = 1000 tick = 1ms

    TMR3CNT = 0;
    TMR3PR  = pr_val;

    // 占空比计算 (手册公式):
    //   高电平长度 = PR - DUTY
    //   DUTY = PR - (PR+1) * 占空比(高)
    //
    //   PWM0: 25% → 高电平 250 tick → DUTY0 = 999 - 250 = 749
    //   PWM1: 50% → 高电平 500 tick → DUTY1 = 999 - 500 = 499
    //   PWM2: 75% → 高电平 750 tick → DUTY2 = 999 - 750 = 249
    TMR3DUTY0 = 749;    // PWM0: 25% 高电平占空比
    TMR3DUTY1 = 499;    // PWM1: 50% 高电平占空比
    TMR3DUTY2 = 249;    // PWM2: 75% 高电平占空比

    // ================================================================
    // 3. 启动 Timer3 PWM
    //    PWM0EN + PWM1EN + PWM2EN + TMREN
    //    INCSEL=00 (tmr_inc 时钟), INCSRC=0 (内部时钟)
    // ================================================================
    TMR3CON = BIT(11)          // PWM2EN
            | BIT(10)          // PWM1EN
            | BIT(9)           // PWM0EN
            | (0 << 2)         // INCSEL=00: tmr_inc
            | BIT(0);          // TMREN: 使能定时器

    // ================================================================
    // 4. 打印配置信息
    // ================================================================
    printf("Timer3 PWM Configuration:\n");
    printf("  Clock source : tmr_inc = 1MHz\n");
    printf("  TMR3PR       = %lu (period = %lu tick = 1ms, f = 1KHz)\n",
           pr_val, pr_val + 1);
    printf("  TMR3DUTY0    = 749 -> PWM0(PB0) 25%% duty (high=250us, low=750us)\n");
    printf("  TMR3DUTY1    = 499 -> PWM1(PB1) 50%% duty (high=500us, low=500us)\n");
    printf("  TMR3DUTY2    = 249 -> PWM2(PB2) 75%% duty (high=750us, low=250us)\n");
    printf("\n");
    printf("Hardware connections (logic analyzer):\n");
    printf("  CH1 -> PB0 (PWM0, 25%%)\n");
    printf("  CH2 -> PB1 (PWM1, 50%%)\n");
    printf("  CH3 -> PB2 (PWM2, 75%%)\n");
    printf("  GND -> GND\n");
    printf("\n===== PWM running, observe with logic analyzer =====\n");

    while (1) {
        delay_ms(2000);
        printf("PWM alive: TMR3CNT=%lu\n", TMR3CNT);
    }
}
