#ifndef _TEST_UART_H_
#define _TEST_UART_H_

// UART 测试入口（UART2, PB2=TX, PB1=RX, 115200 8N1）
// 自 smart_mini_copilot 移植：原 UART1(PA6/PA7) 路径因该口物理损坏已弃用
//
// test_uart_run()       : 回环测试（跳线 PB2<->PB1，自发自收 256 字节）—— 默认
// test_uart2_send_run() : 持续发送（PB2 TX → USB-TTL RX → PC 串口助手）
// test_uart2_recv_run() : 持续接收（PC → USB-TTL TX → PB1 RX，经 UART0 打印）
// test_uart2_console_run(): printf 重定向到 UART2，PC 敲字符经 UART2 收发回显（收发一起测）
void test_uart_run(void);
void test_uart2_send_run(void);
void test_uart2_recv_run(void);
void test_uart2_console_run(void);

#endif // _TEST_UART_H_
