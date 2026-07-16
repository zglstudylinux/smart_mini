#ifndef _TEST_UART_H_
#define _TEST_UART_H_

// UART1 测试入口
// 使用 UART1 Group G2 = PA3(RX) + PA4(TX)
// 不影响 PB3 的 UART0 debug 打印
void test_uart_run(void);

#endif // _TEST_UART_H_