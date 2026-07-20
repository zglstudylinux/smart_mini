/**
 * @file    test_uart.c
 * @brief   BT892X UART2 测试 (回环 / 发送 / 接收) —— 自 smart_mini_copilot 移植，功能不变
 *
 * 测试函数:
 *   test_uart_run()        - 回环测试: 跳线 PB2<->PB1, 自发自收 256 字节（默认入口）
 *   test_uart2_send_run()  - 发送测试: PB2(TX) → USB转TTL → PC串口助手
 *   test_uart2_recv_run()  - 接收测试: PC串口助手 → USB转TTL → PB1(RX)
 *
 * 说明: 原 UART1(PA6/PA7 G1) 路径因该口在开发板上物理损坏，已整体换成 UART2(PB1/PB2 G2)。
 * ⚠️ PB1/PB2 与 TMR3 PWM 冲突：TEST_UART_EN 不能与 TEST_TIMER_PWM_EN 同开。
 *
 * 参考手册: BT892X_UserManual_Driver.md §6 UART
 * 引脚手册: bt892x_pinfunction.md §4.2 PORTB / §5.1 UART2
 */

#include "test_common.h"

/* UART2 初始化 (115200, 8N1, PB2=TX, PB1=RX) */
static void uart2_test_init(void)
{
    // UART2 引脚映射: G2 (FUNCMCON1[7:4]=UT2TXMAP, [11:8]=UT2RXMAP)
    FUNCMCON1 &= ~((0xF << 4) | (0xF << 8));
    FUNCMCON1 |= (2 << 4) | (2 << 8);           // TX=PB2, RX=PB1 (G2)

    // PB2 → UART2 TX
    GPIOBFEN |=  BIT(2);
    GPIOBDE  |=  BIT(2);
    GPIOBDIR &= ~BIT(2);
    GPIOBPU  |=  BIT(2);

    // PB1 → UART2 RX
    GPIOBFEN |=  BIT(1);
    GPIOBDE  |=  BIT(1);
    GPIOBDIR |=  BIT(1);
    GPIOBPU  |=  BIT(1);

    // 波特率 115200 (24MHz / 115200 - 1 = 207)
    u32 baud_val = 207;
    UART2BAUD = (baud_val << 16) | baud_val;

    // RXEN + UTEN
    UART2CON = BIT(7) | BIT(0);
    delay_ms(10);

    // ★ 冲洗 RX: UART 使能后可能有毛刺/垃圾数据，丢弃掉
    while (UART2CON & BIT(9)) {
        (void)UART2DATA;
    }
}

/* 发送一个字节 (阻塞，等发送完成才返回) */
static void uart2_putc(u8 ch)
{
    while (!(UART2CON & BIT(8)));   // 等待 TX 空闲
    UART2DATA = ch;                  // 写入数据，开始发送
    while (!(UART2CON & BIT(8)));   // ★ 等待发送完成 (TXPND 重新变 1)
}

/* 接收一个字节 (阻塞) */
static u8 uart2_getc(void)
{
    while (!(UART2CON & BIT(9)));   // 等待 RXPND=1 (接收到数据)
    return (u8)UART2DATA;
}

// ================================================================
//  测试 1: 回环测试 (跳线 PB2 ↔ PB1) —— 默认入口
// ================================================================
void test_uart_run(void)
{
    printf("\n===== BT892X UART2 Loopback Test =====\n\n");

    uart2_test_init();

    printf("UART2: TX=PB2, RX=PB1, 115200bps 8N1\n");
    printf("Jumper: PB2(TX) <-> PB1(RX)\n\n");

    int errors = 0, total = 0;
    for (int val = 0; val <= 0xFF; val++) {
        uart2_putc((u8)val);

        u32 timeout = 0xFFFFF;
        while (!(UART2CON & BIT(9))) {
            if (--timeout == 0) { printf("RX timeout!\n"); break; }
        }

        u8 rx = (u8)UART2DATA;
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
//  测试 2: 持续发送 (PB2 TX → USB转TTL → PC 串口助手)
//  接线: PB2 → USB-TTL RX, GND → GND
// ================================================================
void test_uart2_send_run(void)
{
    printf("\n===== BT892X UART2 Send Test =====\n\n");

    uart2_test_init();

    printf("UART2 TX = PB2, 115200bps 8N1\n");
    printf("Wiring: PB2 -> USB-TTL RX, GND -> GND\n");
    printf("Open serial monitor @ 115200, 8N1\n\n");

    u32 count = 0;
    while (1) {
        // 发送递增计数 + 测试字符串
        uart2_putc('\r');
        uart2_putc('\n');
        uart2_putc('[');
        // 手动打印数字 (避免依赖 printf 重定向)
        char buf[16];
        int i = 0;
        u32 n = count;
        if (n == 0) buf[i++] = '0';
        while (n) { buf[i++] = '0' + (n % 10); n /= 10; }
        while (i > 0) uart2_putc(buf[--i]);

        uart2_putc(']');
        uart2_putc(' ');
        uart2_putc('H');
        uart2_putc('e');
        uart2_putc('l');
        uart2_putc('l');
        uart2_putc('o');
        uart2_putc(' ');
        uart2_putc('U');
        uart2_putc('A');
        uart2_putc('R');
        uart2_putc('T');
        uart2_putc('2');
        uart2_putc('!');

        count++;
        delay_ms(500);
    }
}

// ================================================================
//  测试 3: 持续接收 (PC 串口助手 → USB转TTL → PB1 RX)
//  接线: USB-TTL TX → PB1, GND → GND
//  收到的数据通过 UART0 printf 打印到串口
// ================================================================
void test_uart2_recv_run(void)
{
    printf("\n===== BT892X UART2 Recv Test =====\n\n");

    uart2_test_init();

    printf("UART2 RX = PB1, 115200bps 8N1\n");
    printf("Wiring: USB-TTL TX -> PB1, GND -> GND\n");
    printf("Send data from PC serial monitor @ 115200\n");
    printf("Received bytes will be printed here:\n\n");

    while (1) {
        u8 ch = uart2_getc();

        // 可打印字符直接显示，控制字符显示十六进制
        if (ch >= 0x20 && ch <= 0x7E) {
            printf("UART2 RX: '%c' (0x%02X)\n", ch, ch);
        } else if (ch == '\r' || ch == '\n') {
            printf("UART2 RX: <CR/LF> (0x%02X)\n", ch);
        } else {
            printf("UART2 RX: 0x%02X\n", ch);
        }
    }
}

// ================================================================
//  测试 4: UART2 控制台/回显 —— printf 重定向到 UART2，收发一起验证
//  接线: PB2(TX) → USB-TTL RX，PB1(RX) ← USB-TTL TX，GND ↔ GND
//  在 PC 串口助手(115200 8N1)里敲字符 → 芯片经 UART2 收到(验证收) →
//  用 printf 回显 → 经重定向由 UART2 发回(验证发 + printf 重定向)
// ================================================================

/* printf 输出回调：把一个字符经 UART2 发出（供 my_printf_init 注册） */
static void uart2_console_putchar(char ch)
{
    uart2_putc((u8)ch);
}

/**
 * @brief  初始化 UART2 + 把 printf 重定向到 UART2 (PB2 TX @ 115200 8N1)
 *
 * 调用后所有 printf 走 UART2/PB2。main.c 想让后续 printfs 全部走 UART2
 * 时调一次即可（不必再走 test_uart2_console_run）。
 *
 * 想换回 UART0/PB3 调 my_printf_init(uart_putchar)。
 */
void uart2_console_init(void)
{
    uart2_test_init();
    my_printf_init(uart2_console_putchar);
}

void test_uart2_console_run(void)
{
    uart2_console_init();

    printf("\r\n===== BT892X UART2 Console (printf -> UART2) =====\r\n");
    printf("UART2: TX=PB2, RX=PB1, 115200bps 8N1\r\n");
    printf("Wiring: PB2->USB-TTL RX, PB1<-USB-TTL TX, GND-GND\r\n");
    printf("Type chars in PC serial monitor; they will be echoed back:\r\n");

    while (1) {
        u8 ch = uart2_getc();      // 硬件 UART2 接收（验证收）
        uart2_putc(ch);            // 原样回显（验证发）
        if (ch == '\r') uart2_putc('\n');
    }
}

