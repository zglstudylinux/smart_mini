#ifndef _TEST_UART_SOFT_H_
#define _TEST_UART_SOFT_H_

// 软件 GPIO 模拟串口 (Bit-Bang UART) 测试入口
// 自 smart_mini_copilot soft_uart_test.c 移植，引脚从 PE4/PE5 改为 PB2/PB1，
// 与硬件 UART2 一致（同一根跳线 PB2<->PB1，只要软/硬不同时开就不冲突）
//   PB2 → 软件 TX (GPIO 输出)
//   PB1 → 软件 RX (GPIO 输入)
// 9600bps 8N1，跳线 PB2<->PB1 回环 256 字节
void test_uart_soft_run(void);

#endif // _TEST_UART_SOFT_H_
