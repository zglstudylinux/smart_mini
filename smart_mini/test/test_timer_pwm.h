#ifndef _TEST_TIMER_PWM_H_
#define _TEST_TIMER_PWM_H_

// Timer3 PWM 测试入口
// TMR3 三路 PWM 同时输出：PWM0→PB0(25%)、PWM1→PB1(50%)、PWM2→PB2(75%)
// 逻辑分析仪验证周期(1kHz)和占空比
void test_timer_pwm_run(void);

#endif // _TEST_TIMER_PWM_H_
