#ifndef _TEST_H_
#define _TEST_H_

#include "include.h"

// ================================================================
// 外部函数声明 (定义在 main.c 中)
// ================================================================
extern u32  tick_get(void);
extern bool tick_check_expire(u32 tick, u32 expire_val);
extern void delay_us(uint nus);
extern void delay_ms(uint n);
extern void delay_5ms(uint n);

// ================================================================
// 测试函数声明
// ================================================================
void gpio_test(void);
void timer_pwm_test(void);
void uart_test(void);
void uart2_send_test(void);
void uart2_recv_test(void);
void soft_uart_test(void);
void soft_spi_test(void);
void soft_i2c_test(void);
void i2c_test(void);

#endif // _TEST_H_
