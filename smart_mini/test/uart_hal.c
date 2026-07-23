/**
 * @file    uart_hal.c
 * @brief   UART HAL 实现 —— 软件 bit-bang + 硬件 UART2 + Console
 *
 *  手册依据（BT892X_UserManual_Driver.md）：
 *    §3.2 GPIO 通用控制寄存器（每步对应 GPIOxFEN/DE/DIR/SET/CLR/读）
 *    §3.3 FUNCMCON1[7:4] UT2TXMAP / [11:8] UT2RXMAP，0010=G2
 *    §6.2 UARTxCON[9] RXIPND / [8] TXIPND / [7] RXEN / [0] UTEN
 *    §6.2 UARTxCPND 写 1 清挂起（read UARTxDATA 不自动清 RXPND）
 */

#include "uart_hal.h"

/* main.c 里的默认 putchar（写 UART0/PB3，调试串口） */
extern void uart_putchar(char ch);

// =====================================================================
//  软件 bit-bang 实现（9600 8N1, TX=PB2 / RX=PB1）
//
//  时序（bit 周期 = 1000000 / baud 微秒）：
//    1. start bit (LOW)
//    2. 8 data bits (LSB-first)，每位 ~104us@9600
//    3. stop bit (HIGH)
//
//  每位：set TX → delay 半 bit → sample RX → delay 半 bit
// =====================================================================

void uart_hal_soft_init(u32 baud)
{
    /* FEN=0 关闭 PB1/PB2 外设功能，强制为普通 GPIO（§3.2） */
    GPIOBFEN &= ~(UART_TX_PIN | UART_RX_PIN);
    GPIOBDE  |=  (UART_TX_PIN | UART_RX_PIN);

    /* ★ 默认 PB1/PB2 都设输入 + 上拉：避免 PB2 输出干扰外接的 RX 源
     *  之前 init 把 PB2 设为输出 idle HIGH，如果还在接 PB2↔PB1 跳线
     *  会和 PC 端 TX 拉 LOW 的 start bit 冲突，线被双 driver 拉锯产生噪声。
     *  软 TX 在 putc 里临时改 DIR=输出，发送完恢复 DIR=输入。
     */
    GPIOBDIR |=  (UART_TX_PIN | UART_RX_PIN);
    GPIOBPU  |=  (UART_TX_PIN | UART_RX_PIN);

    /* bit 周期由 baud 决定；只存到 static 让 putc/getc 用 */
    /* （这里 baud 实际不存，固定 9600 简化测试；如需可变加 static） */
    (void)baud;

    /* ★ 还原 printf 到默认 UART0（防止 console 测试残留的 my_printf_init 改写） */
    my_printf_init(uart_putchar);
}

/* 内部：每 bit 延时（9600 → 104µs；半 bit 52µs） */
#define SOFT_BIT_US  104
#define SOFT_HALF_US 52

void uart_hal_soft_putc(u8 tx)
{
    u8 i;

    /* 临时设 PB2 输出（init 默认是输入，避免干扰外接 RX） */
    GPIOBDIR &= ~UART_TX_PIN;
    GPIOBSET  = UART_TX_PIN;   /* idle HIGH */

    /* start bit (LOW) */
    GPIOBCLR = UART_TX_PIN;
    delay_us(SOFT_BIT_US);

    /* 8 data bits, LSB-first */
    for (i = 0; i < 8; i++) {
        if (tx & (1u << i)) GPIOBSET = UART_TX_PIN;
        else                GPIOBCLR = UART_TX_PIN;   /* §3.2: write 1 to clear */
        delay_us(SOFT_BIT_US);
    }

    /* stop bit (HIGH) */
    GPIOBSET = UART_TX_PIN;
    delay_us(SOFT_BIT_US);

    /* 恢复 PB2 为输入（避免外接 RX 源被本地驱动干扰） */
    GPIOBDIR |=  UART_TX_PIN;
}

u8 uart_hal_soft_getc(void)
{
    u8 i, rx = 0;

    /* 等 start bit 下降沿（高 → 低） */
    while (GPIOB & UART_RX_PIN);              /* 等待 LOW（active） */
    delay_us(SOFT_HALF_US);                  /* 延迟半位到中心采样点 */

    /* 8 data bits, LSB-first */
    for (i = 0; i < 8; i++) {
        if (GPIOB & UART_RX_PIN) rx |= (1u << i);
        delay_us(SOFT_BIT_US);
    }

    /* stop bit 验证（应在中心为 HIGH；不在此处阻塞） */
    delay_us(SOFT_HALF_US);

    return rx;
}

/* 同步发送+接收一个字节（loopback 专用）
 * 边发 TX 边在每 bit 中心采样 RX —— 这是真正能 loopback 的方式
 * 不要用 putc+getc 组合：putc 完线停在 stop HIGH，getc 等 start bit 永远不来
 *
 * ★ 关键修复：init() 默认把 PB2 设输入（避免双 driver 噪声），所以 txrx
 *   第一件事必须把 DIR 切到输出，否则 GPIOBCLR 写到锁存器但不驱动引脚，
 *   RX 端读到的是恒高，0xFF 全错。完事再切回输入保持默认。
 */
u8 uart_hal_soft_txrx(u8 tx)
{
    u8 i, rx = 0;

    /* 切到输出驱动 TX —— 没这一步 GPIOBCLR 不出引脚 */
    GPIOBDIR &= ~UART_TX_PIN;
    GPIOBSET  = UART_TX_PIN;                  /* idle HIGH */

    /* start bit LOW */
    GPIOBCLR = UART_TX_PIN;
    delay_us(SOFT_HALF_US);                   /* 延迟半位到中心采样（注意：start bit 中心仍为 LOW，不采样） */
    delay_us(SOFT_HALF_US);                   /* 跨过整个 start bit */

    /* 8 data bits, LSB-first —— 同步发+采 */
    for (i = 0; i < 8; i++) {
        if (tx & (1u << i)) GPIOBSET = UART_TX_PIN;
        else                GPIOBCLR = UART_TX_PIN;
        delay_us(SOFT_HALF_US);               /* 延迟到 bit 中心 */
        if (GPIOB & UART_RX_PIN) rx |= (1u << i);
        delay_us(SOFT_HALF_US);               /* 跨过整个 bit */
    }

    /* stop bit HIGH */
    GPIOBSET = UART_TX_PIN;
    delay_us(SOFT_HALF_US);                   /* stop bit 中心采样（应在 HIGH） */

    /* 恢复 PB2 为输入（与 init 默认一致，避免与外接 RX 源双 driver 拉锯） */
    GPIOBDIR |= UART_TX_PIN;

    return rx;
}

/* 注：上面在 putc 用 GPIOBSET/GPIOBCLR 是 §3.2 标准 SFR 写入；
 *  GPIOBSET/GPIOBCLR 宏定义在 sfr.h PB 端口段 */

// =====================================================================
//  硬件 UART2 实现（G2 = PB2/TX, PB1/RX, 115200 8N1）
// =====================================================================

void uart_hal_hw_init(u32 baud)
{
    /* FUNCMCON1 G2 映射：UT2TXMAP=G2 (PB2), UT2RXMAP=G2 (PB1) */
    FUNCMCON1 &= ~((0xF << 4) | (0xF << 8));
    FUNCMCON1 |=  (2 << 4) | (2 << 8);

    /* PB2 → UART2 TX（功能 IO 输出） */
    GPIOBFEN |=  UART_TX_PIN;
    GPIOBDE  |=  UART_TX_PIN;
    GPIOBDIR &= ~UART_TX_PIN;
    GPIOBPU  |=  UART_TX_PIN;

    /* PB1 → UART2 RX（功能 IO 输入） */
    GPIOBFEN |=  UART_RX_PIN;
    GPIOBDE  |=  UART_RX_PIN;
    GPIOBDIR |=  UART_RX_PIN;
    GPIOBPU  |=  UART_RX_PIN;

    /* 波特率（手册 §6.2: BAUD = Fsys / (BAUD+1)）
     * HAL 接口接实际波特率（如 115200），内部算 divisor = (Fsys/baud) - 1
     * 24MHz / 115200 = 208.33 → divisor=207（向下取整得 115384 波特，误差 0.16%）
     */
    u32 baud_val = (24000000 + baud/2) / baud - 1;
    UART2BAUD = (baud_val << 16) | baud_val;

    /* RXEN + UTEN */
    UART2CON = BIT(7) | BIT(0);
    delay_ms(10);

    /* ★ 冲洗 RX：使能后可能有毛刺/垃圾数据，丢弃（连同清挂起） */
    while (UART2CON & BIT(9)) {
        (void)UART2DATA;
        UART2CPND = BIT(9);    /* 显式清 RXPND，否则会重复 */
    }

    /* ★ 还原 printf 到默认 UART0（PB3 调试串口）
     * 防止上一个 test（如 console）my_printf_init 改写了全局函数指针后残留。
     * 不还原的话本测试的 printf 输出会发到 UART2 而不是 UART0，PC 看板上就无声。
     */
    my_printf_init(uart_putchar);
}

void uart_hal_hw_putc(u8 tx)
{
    while (!(UART2CON & BIT(8)));   /* 等 TX 空闲（§6.2 TXIPND=1） */
    UART2DATA = tx;
    while (!(UART2CON & BIT(8)));   /* 等发送完成（TXIPND 重新置 1） */
}

u8 uart_hal_hw_getc(void)
{
    u8 ch;

    /* ★ 防御性双清：万一上次残留 RXPND（init 后第一个字节、PC 突发、
     *   上一轮 CPND 写被新到达覆盖等），先把 PND 清掉再等，
     *   避免读到陈旧 DATA / 错过新字节。
     *   §6.2 L421-428：写 UART2CPND bit9=1 清 RXPND。
     */
    UART2CPND = BIT(9);

    while (!(UART2CON & BIT(9)));   /* 等 RXIPND=1 */
    ch = (u8)UART2DATA;
    UART2CPND = BIT(9);              /* 读后再次清挂起，防止下一轮 while 误通过 */
    return ch;
}

// =====================================================================
//  Console / printf 重定向层
// =====================================================================

void uart_hal_console_init(u32 baud)
{
    uart_hal_hw_init(baud);

    /* ★ 重定向 printf -> UART2 后再打印 banner，让 banner 出现在 PC 串口上
     *   （用户在 console 测试场景下已经把 USB-TTL 接到 PB2/PB1，
     *    UART0/PB3 通常没接，看不到 UART0 输出的 banner 会被误以为"卡死"）。
     *   副作用：header 之后所有 printf（含 echo loop 内的）都从 UART2 出。
     */
    my_printf_init(uart_hal_console_putchar);
    printf("\r\n===== BT892X UART2 Console (printf -> UART2) =====\r\n");
    printf("UART2: TX=PB2, RX=PB1, 115200bps 8N1\r\n");
    printf("Wiring: PB2->USB-TTL RX, PB1<-USB-TTL TX, GND-GND\r\n");
    printf("Type chars in PC serial monitor; they will be echoed back:\r\n");
}

void uart_hal_console_putchar(char ch)
{
    uart_hal_hw_putc((u8)ch);
}

u8 uart_hal_console_getc(void)
{
    return uart_hal_hw_getc();
}
