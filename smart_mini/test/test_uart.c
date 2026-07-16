// UART1 测试 — 寄存器级验证 PA6(RX) + PA7(TX) 双线通信
// 手册：BT892X_UserManual_Driver.md §3.3 (FUNCMCON0 UART1 映射) + §6 (UART)
// 引脚定义：docs/bt892x_pinfunction.md §4.1 (PA6=RX1-G1, PA7=TX1-G1)
// SFR：见 header/sfr.h 第 88-92 行（UART1CON/CPND/BAUD/DATA）
//
// 引脚选择理由：
//   - 原本计划用 PA3/PA4 (G2)，但 PA4 物理测试 GPIO 输出无信号（诊断失败）
//   - 改用 PA6/PA7 (G1)：手册标注完全空闲，PA7 原本是 UART0 默认 TX
//     但 main.c 已把 UART0 重映射到 PB3，所以 PA7 现在空闲
//   - 不动 PB3 的 UART0 debug 打印
//
// 测试内容：
//   1. TX-only：发送字符串，逻辑分析仪在 PA7 看波形
//   2. Loopback：短接 PA6↔PA7，发送并比较接收数据
//   3. Poll-RX：等待外部输入的字节（验证 RX 中断通路）

#include "test_common.h"

// ===== 波特率计算（24 MHz 系统时钟）=====
// BAUD = Fsys / baud_rate - 1
#define UART1_BAUD_115200   ((24000000 / 115200) - 1)   // = 207
#define UART1_BAUD_9600     ((24000000 / 9600) - 1)     // = 2499

// PA6 = BIT(6), PA7 = BIT(7)
#define PA6_MASK    BIT(6)
#define PA7_MASK    BIT(7)
#define PA6_7_MASK  (PA6_MASK | PA7_MASK)

// 初始化 UART1：配置 GPIO + 映射 + 波特率 + 使能
// baud：目标波特率
static void test_uart1_init(u32 baud_div)
{
    // 1. 先清除可能的残留映射（写 0xF 到 UT1TXMAP 和 UT1RXMAP 字段）
    FUNCMCON0 = (FUNCMCON0 & 0x00ffffff) | (0xfu << 24) | (0xfu << 28);

    // 2. PA6/PA7 PAD 配置
    GPIOADE  |=  PA6_7_MASK;   // 数字 IO 使能
    GPIOAFEN |=  PA6_7_MASK;   // 外设功能映射
    GPIOAPU  |=  PA6_MASK;     // PA6 (RX) 上拉（避免悬空误触发）
    GPIOADIR |=  PA6_MASK;     // PA6 = 输入（RX）
    GPIOADIR &= ~PA7_MASK;     // PA7 = 输出（TX）

    // 3. 映射 UART1 到 G1（PA6=RX, PA7=TX）
    //    FUNCMCON0[27:24] = UT1TXMAP = 0x1 (G1)
    //    FUNCMCON0[31:28] = UT1RXMAP = 0x1 (G1)
    FUNCMCON0 = (FUNCMCON0 & 0x00ffffff) | (1u << 24) | (1u << 28);

    // 4. 设置波特率（高 16 位 = RX，低 16 位 = TX）
    UART1BAUD = (baud_div << 16) | baud_div;

    // 5. 使能 UART1 + 接收
    //    BIT(0) = UTEN, BIT(7) = RXEN
    UART1CON = BIT(7) | BIT(0);

    // 6. 【谨慎】只清 RXPND，不清 TXPND！
    //    原因：清 TXPND 会让 UART1CON BIT(8) 变成 0，
    //    导致后续 test_uart1_tx_byte 的等待永远不返回
    //    RXPND 在使能后应该是 0（无数据），但清除一下更稳妥
    UART1CPND = BIT(9);    // 仅清 RXPND

    // 7. 短暂等待稳定
    delay_us(100);
}

// 发送 1 字节（轮询 TX 准备好）
static void test_uart1_tx_byte(u8 ch)
{
    while (!(UART1CON & BIT(8)));   // 等待 TX ready（bit8 = TXPND 反向）
    UART1CPND = BIT(8);             // 清 TX pending
    UART1DATA = ch;
}

// 接收 1 字节（轮询，带超时）
// timeout_us：超时（µs）
// 返回：成功返回字节值，超时返回 0xFFFFFFFF
static u32 test_uart1_rx_byte(u32 timeout_us)
{
    u32 t0 = TMR2CNT;
    while (!(UART1CON & BIT(9))) {  // 等待 RX ready（bit9 = RXPND）
        if ((u32)(TMR2CNT - t0) > timeout_us) {
            return 0xFFFFFFFFul;  // 超时
        }
    }
    UART1CPND = BIT(9);             // 清 RX pending
    return (u32)(UART1DATA & 0xFF);
}

void test_uart_run(void)
{
    TEST_LOG("========================================");
    TEST_LOG("UART1 test start (G1: PA6=RX, PA7=TX)");
    TEST_LOG("========================================");

    // ===== Test 1: TX-only @ 115200 =====
    TEST_LOG("[Test 1] TX-only @ 115200 baud");
    TEST_LOG("  Connect logic analyzer to PA7 to see waveform");
    test_uart1_init(UART1_BAUD_115200);

    {
        const char *msg = "UART1 TX TEST\r\n";
        while (*msg) {
            test_uart1_tx_byte((u8)*msg++);
        }
        TEST_LOG("  TX sent: 'UART1 TX TEST' (14 bytes)");
    }

    delay_ms(100);

    // ===== Test 2: Loopback @ 115200 =====
    TEST_LOG("[Test 2] Loopback test @ 115200 baud");
    TEST_LOG("  Short PA6 <-> PA7 externally");
    delay_ms(2000);    // 给用户 2 秒时间接杜邦线

    test_uart1_init(UART1_BAUD_115200);

    {
        const char *tx_msg = "Hello UART1";
        const u32 tx_len = 11;
        u8 rx_buf[16] = {0};
        u32 err_cnt = 0;

        TEST_LOG("  Sending 'Hello UART1' (11 bytes)...");

        // 发送
        for (u32 i = 0; i < tx_len; i++) {
            test_uart1_tx_byte((u8)tx_msg[i]);
        }

        // 【关键】等最后一字节物理线上发送完成
        // 之前只等缓冲器可写，不等于物理线上的位流已发出
        while (!(UART1CON & BIT(8)));   // TXPND=1 表示 TX buffer 空 + shift reg 空
        UART1CPND = BIT(8);             // 清 TXPND

        // 再等 1ms 让 RX shift register 把回环数据搬进来
        delay_ms(1);

        // 接收（带超时）
        for (u32 i = 0; i < tx_len; i++) {
            u32 rx = test_uart1_rx_byte(100000);   // 100ms 超时
            if (rx == 0xFFFFFFFFul) {
                TEST_LOG("  [TIMEOUT] RX byte %u timeout", (u32)i);
                err_cnt++;
                break;
            }
            rx_buf[i] = (u8)rx;
        }

        // 验证
        rx_buf[tx_len] = '\0';
        TEST_LOG("  RX got: '%s'", rx_buf);

        u32 match_cnt = 0;
        for (u32 i = 0; i < tx_len; i++) {
            if (rx_buf[i] == (u8)tx_msg[i]) match_cnt++;
        }

        if (match_cnt == tx_len && err_cnt == 0) {
            TEST_LOG("  Loopback PASS (%u/%u bytes match)", match_cnt, tx_len);
        } else {
            TEST_LOG("  Loopback FAIL (%u/%u match, %u timeouts)",
                     match_cnt, tx_len, err_cnt);
        }
    }

    // ===== Test 3: Poll-RX @ 9600 =====
    TEST_LOG("[Test 3] Poll-RX @ 9600 baud");
    TEST_LOG("  Send any byte from external device to PA6 (RX)");

    test_uart1_init(UART1_BAUD_9600);

    {
        u32 t0 = TMR2CNT;
        u32 timeout_ms = 5000;     // 5 秒超时
        u32 rx = 0xFFFFFFFFul;
        while ((u32)(TMR2CNT - t0) < timeout_ms * 1000) {
            if (UART1CON & BIT(9)) {
                UART1CPND = BIT(9);
                rx = (u32)(UART1DATA & 0xFF);
                break;
            }
        }
        if (rx == 0xFFFFFFFFul) {
            TEST_LOG("  [TIMEOUT] No byte received in 5s (skip if no external source)");
        } else {
            TEST_LOG("  RX got: 0x%02x ('%c')", (u32)rx,
                     (rx >= 32 && rx < 127) ? rx : '?');
        }
    }

    TEST_LOG("========================================");
    TEST_LOG("UART1 test done");
    TEST_LOG("========================================");

    while (1);
}