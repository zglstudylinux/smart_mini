#ifndef _TEST_TIMER_H_
#define _TEST_TIMER_H_

// Timer 测试入口
// 验证 TMR1 的精度（轮询模式）和中断能力（ISR 模式）
// TMR0/TMR2 已被 main.c 占用，本测试只能用 TMR1
void test_timer_run(void);

#endif // _TEST_TIMER_H_