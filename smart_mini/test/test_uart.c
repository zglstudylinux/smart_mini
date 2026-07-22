/**
 * @file    test_uart.c
 * @brief   BT892X UART 测试 —— 软/硬各一份，测试入口都基于 uart_hal 原语
 *
 * 所有寄存器 / GPIO / 波特率配置都封装在 [uart_hal.c](uart_hal.c) 里，
 * 本文件只负责"跑测试场景"，不复写任何 init/putc/getc。
 *
 * 测试入口:
 *   test_uart_run()        - 硬件 UART2 回环 (跳线 PB2<->PB1, 256 字节)
 *   test_uart2_send_run()  - 硬件 UART2 持续发送 (PB2 TX -> USB-TTL -> PC)
 *   test_uart2_recv_run()  - 硬件 UART2 持续接收 (PC -> USB-TTL -> PB1 RX, 行缓冲)
 *   test_uart2_console_run()- 硬件 UART2 收发回显 + printf 重定向到 UART2
 *   test_uart_soft_run()   - 软件 bit-bang UART 回环 (跳线 PB2<->PB1, 9600 8N1)
 *
 * ⚠️ PB1/PB2 与 TMR3 PWM 冲突：UART 各测试 不能与 TEST_TIMER_PWM_EN 同开。
 * ⚠️ 软/硬 UART 测试不能同开（同一对引脚 PB1/PB2，互斥）
 *
 * 参考手册: BT892X_UserManual_Driver.md §6 UART
 * 引脚手册: bt892x_pinfunction.md §4.2 PORTB / §5.1 UART2
 */

#include "test_common.h"
#include "uart_hal.h"

// ================================================================
//  测试 1: 硬件 UART2 回环 (跳线 PB2 ↔ PB1) —— 默认入口
// ================================================================
void test_uart_run(void)
{
    printf("\n===== BT892X UART2 Loopback Test =====\n\n");

    uart_hal_hw_init(115200);

    printf("UART2: TX=PB2, RX=PB1, 115200bps 8N1\n");
    printf("Jumper: PB2(TX) <-> PB1(RX)\n\n");

    int errors = 0, total = 0;
    for (int val = 0; val <= 0xFF; val++) {
        uart_hal_hw_putc((u8)val);
        u8 rx = uart_hal_hw_getc();
        total++;
        if (rx != (u8)val) {
            errors++;
            if (errors <= 5) printf("ERR: sent=0x%02X rx=0x%02X\n", val, rx);
        }
    }

    printf("\n===== Result =====\nTotal: %d, Errors: %d\n", total, errors);
    if (errors == 0) printf("ALL PASSED!\n");
    while (1);
}

// ================================================================
//  测试 2: 硬件 UART2 持续发送 (PB2 TX -> USB-TTL -> PC 串口助手)
//  接线: PB2 -> USB-TTL RX, GND -> GND
// ================================================================
void test_uart2_send_run(void)
{
    printf("\n===== BT892X UART2 Send Test =====\n\n");

    uart_hal_hw_init(115200);

    printf("UART2 TX = PB2, 115200bps 8N1\n");
    printf("Wiring: PB2 -> USB-TTL RX, GND -> GND\n");
    printf("Open serial monitor @ 115200, 8N1\n\n");

    u32 count = 0;
    while (1) {
        // 发送递增计数 + 测试字符串
        uart_hal_hw_putc('\r');
        uart_hal_hw_putc('\n');
        uart_hal_hw_putc('[');
        // 手动打印数字 (避免依赖 printf 重定向)
        char buf[16];
        int i = 0;
        u32 n = count;
        if (n == 0) buf[i++] = '0';
        while (n) { buf[i++] = '0' + (n % 10); n /= 10; }
        while (i > 0) uart_hal_hw_putc(buf[--i]);

        uart_hal_hw_putc(']');
        uart_hal_hw_putc(' ');
        uart_hal_hw_putc('H');
        uart_hal_hw_putc('e');
        uart_hal_hw_putc('l');
        uart_hal_hw_putc('l');
        uart_hal_hw_putc('o');
        uart_hal_hw_putc(' ');
        uart_hal_hw_putc('U');
        uart_hal_hw_putc('A');
        uart_hal_hw_putc('R');
        uart_hal_hw_putc('T');
        uart_hal_hw_putc('2');
        uart_hal_hw_putc('!');

        count++;
        delay_ms(500);
    }
}

// ================================================================
//  测试 3: 硬件 UART2 持续接收 (PC 串口助手 -> USB-TTL -> PB1 RX)
//  接线: USB-TTL TX -> PB1, GND -> GND
//  收到的数据按行缓存，收到 '\n' 或缓冲满时整行通过 UART0 printf 输出。
//  这样可以避免每字节一行造成终端刷屏，并支持显示完整字符串。
// ================================================================
#define UART2_RECV_LINE_MAX  64u

void test_uart2_recv_run(void)
{
    printf("\n===== BT892X UART2 Recv Test =====\n\n");

    uart_hal_hw_init(115200);

    printf("UART2 RX = PB1, 115200bps 8N1\n");
    printf("Wiring: USB-TTL TX -> PB1, GND -> GND\n");
    printf("Send data from PC serial monitor @ 115200\n");
    printf("Type a line + Enter; press Ctrl+C (0x03) to leave the test\n");
    printf("Received lines will be printed here:\n\n");

    u8  line_buf[UART2_RECV_LINE_MAX + 1u];
    u32 line_len = 0;
    u32 rx_count = 0;
    u32 line_count = 0;

    while (1) {
        u8 ch = uart_hal_hw_getc();

        /* ★ 每字节立即 echo:hex 字节 + 可显字符(不可显显示 '.')
         *   这样发单个字节(hex send 工具)也能立刻看到反馈,不用等 Enter。
         *   行模式用户照样能在 \r/\n 后看到 "UART2 RX line #N: ..." 整行汇总。*/
        if (ch >= 0x20u && ch < 0x7Fu) {
            printf("RX[%03u] 0x%02X '%c'\n", (u32)rx_count, ch, ch);
        } else {
            printf("RX[%03u] 0x%02X\n", (u32)rx_count, ch);
        }

        // 0x03 (Ctrl+C) 立即退出测试，保留已收数据。
        if (ch == 0x03u) {
            printf("\n[Recv] Exit requested (0x03)\n");
            break;
        }

        // 收到回车/换行即整行输出。
        if (ch == '\r' || ch == '\n') {
            if (line_len > 0) {
                line_buf[line_len] = '\0';
                printf("UART2 RX line #%u: \"%s\" (len=%u)\n",
                       (u32)line_count, (const char *)line_buf,
                       (u32)line_len);
                line_count++;
            } else {
                printf("UART2 RX line #%u: <CR/LF only>\n",
                       (u32)line_count);
                line_count++;
            }
            line_len = 0;
            continue;
        }

        // 退格：删除前一个字符（终端习惯）。
        if (ch == 0x08u || ch == 0x7Fu) {
            if (line_len > 0) {
                line_len--;
            }
            continue;
        }

        // 其它控制字符丢弃，不进入缓冲区，避免后续格式串误读。
        if (ch < 0x20u) {
            continue;
        }

        if (line_len < UART2_RECV_LINE_MAX) {
            line_buf[line_len++] = ch;
        } else {
            // 行缓冲满：立即 flush 一次，再把当前字符作为新行起点。
            line_buf[line_len] = '\0';
            printf("UART2 RX line #%u (overflow): \"%s\"\n",
                   (u32)line_count, (const char *)line_buf);
            line_count++;
            line_len = 0;
            line_buf[line_len++] = ch;
        }

        rx_count++;
    }

    // 退出前 flush 残余数据。
    if (line_len > 0) {
        line_buf[line_len] = '\0';
        printf("UART2 RX tail: \"%s\" (len=%u)\n",
               (const char *)line_buf, (u32)line_len);
    }
    printf("[Recv] Total bytes: %u, total lines: %u\n",
           (u32)rx_count, (u32)line_count);
    while (1);
}

// ================================================================
//  测试 4: 硬件 UART2 控制台/回显 —— printf 重定向到 UART2
//  接线: PB2(TX) -> USB-TTL RX，PB1(RX) <- USB-TTL TX，GND <-> GND
//  在 PC 串口助手(115200 8N1)里敲字符 -> 芯片经 UART2 收到 ->
//  回显 -> 经重定向由 UART2 发回（收发一起测）
// ================================================================
void test_uart2_console_run(void)
{
    uart_hal_console_init(115200);   // 已自带 banner，从 UART2/PB2 出

    while (1) {
        u8 ch = uart_hal_console_getc();   // 硬件 UART2 接收（验证收）
        uart_hal_console_putchar((char)ch); // 原样回显（验证发 + printf 重定向）
        if (ch == '\r') uart_hal_console_putchar('\n');
    }
}

// ================================================================
//  测试 5: 软件 bit-bang UART 回环 (跳线 PB2 ↔ PB1, 9600 8N1)
//  用 uart_hal_soft_txrx() 边发边采 —— 不依赖硬件 UART2，不依赖中断。
//  同样测试 0x00~0xFF 256 字节验证全字节通路。
// ================================================================
void test_uart_soft_run(void)
{
    printf("\n===== BT892X Soft UART (bit-bang) Loopback Test =====\n\n");

    uart_hal_soft_init(9600);

    printf("SOFT UART: TX=PB2, RX=PB1, 9600bps 8N1 (FEN=0, GPIO bit-bang)\n");
    printf("Jumper: PB2(TX) <-> PB1(RX)\n\n");

    int errors = 0, total = 0;
    for (int val = 0; val <= 0xFF; val++) {
        u8 rx = uart_hal_soft_txrx((u8)val);
        total++;
        if (rx != (u8)val) {
            errors++;
            if (errors <= 5) printf("ERR: sent=0x%02X rx=0x%02X\n", val, rx);
        }
    }

    printf("\n===== Result =====\nTotal: %d, Errors: %d\n", total, errors);
    if (errors == 0) printf("ALL PASSED!\n");
    while (1);
}