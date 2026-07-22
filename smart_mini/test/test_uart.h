#ifndef _TEST_UART_H_
#define _TEST_UART_H_

// UART 测试入口（软/硬共用 PB2=TX / PB1=RX，一次只开一个）
//
// 硬件 UART2 (115200 8N1):
//   test_uart_run()        - 回环 (跳线 PB2<->PB1, 256 字节自发自收)
//   test_uart2_send_run()  - 持续发送 (PB2 TX -> USB-TTL -> PC 串口助手)
//   test_uart2_recv_run()  - 持续接收 (PC -> USB-TTL -> PB1 RX, 行缓冲打印)
//   test_uart2_console_run()- 收发回显 + printf 重定向到 UART2
//
// 软件 bit-bang (9600 8N1, GPIO 实现):
//   test_uart_soft_run()   - 回环 (跳线 PB2<->PB1, 256 字节自发自收)
//
// ⚠️ PB1/PB2 与 TMR3 PWM 冲突：UART 测试不能与 TEST_TIMER_PWM_EN 同开。
// ⚠️ 软/硬 UART 测试也不能同开（同一对引脚）。
// 原 UART1(PA6/PA7) 路径因该口物理损坏已弃用。
void test_uart_run(void);
void test_uart2_send_run(void);
void test_uart2_recv_run(void);
void test_uart2_console_run(void);
void test_uart_soft_run(void);

#endif // _TEST_UART_H_