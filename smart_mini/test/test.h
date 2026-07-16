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
void hw_spi_test(void);
void hw_exp1_jedec_id(void);
void hw_exp2_status(void);
void hw_exp3_page_rw(void);
void hw_exp4_cross_page(void);
void hw_exp5_sector_erase(void);
void hw_exp6_erase_timing(void);
void hw_exp7_write_protect(void);
void hw_exp8_fast_read(void);
void hw_exp9_speed_demo(void);
void hw_exp10_unique_id(void);
void asm_spi_test(void);
void dma_spi_test(void);
void spi_int_test(void);
void w25q64_exp1_jedec_id(void);
void w25q64_exp2_status(void);
void w25q64_exp3_page_rw(void);
void w25q64_exp4_cross_page(void);
void w25q64_exp5_sector_erase(void);
void w25q64_exp6_erase_timing(void);
void w25q64_exp7_write_protect(void);
void w25q64_exp9_unique_id(void);
void i2c_test(void);

#endif // _TEST_H_
