#include "include.h"
#include "test/test_gpio.h"
#include "test/test_timer.h"
#include "test/test_timer_pwm.h"
#include "test/test_uart.h"
#include "test/test_spi_loop.h"
#include "test/test_spi_wave.h"
#include "test/test_spi_w25q64.h"
#include "test/test_spi_soft_asm.h"
#include "test/test_spi_timing.h"
#include "test/test_i2c.h"
#include "test/test_i2c_la.h"
#include "test/test_i2c_gpio.h"
#include "test/test_i2c_gpio_la.h"
#include "test/test_adkey.h"

#define UART_BAUD           1500000
#define UART_BAUD_VAL       (((24000000 + (UART_BAUD / 2)) / UART_BAUD) - 1)

extern u32 __bss_start, __bss_size, __aram_start;
extern u32 __comm_vma, __comm_lma, __comm_size;

// Test entry declarations (each guarded by its own ifdef)
extern void test_gpio_run(void);
extern void test_timer_run(void);
extern void test_timer_pwm_run(void);
extern void test_uart_run(void);
extern void test_uart2_send_run(void);
extern void test_uart2_recv_run(void);
extern void test_uart2_console_run(void);
extern void test_uart_soft_run(void);
extern void test_spi_loop_run(void);
extern void test_spi_wave_run(void);
extern void test_spi_w25q64_run(void);
extern void test_spi_soft_asm_run(void);
extern void test_spi_timing_run(void);
extern void test_i2c_run(void);
extern void test_i2c_la_run(void);
extern void test_i2c_gpio_run(void);
extern void test_i2c_gpio_la_run(void);
extern void test_adkey_raw_run(void);
extern void test_adkey_map_run(void);
extern void test_adkey_debounce_run(void);
extern void test_adkey_long_run(void);
extern void test_adkey_hold_run(void);

// ===== Test enable switches (enable ONE at a time) =====
// Default: hardware I2C + AT24C02 functional test
// Comment out the default and uncomment one of the others to switch
// #define TEST_GPIO_EN    1
// #define TEST_TIMER_EN   1
// #define TEST_TIMER_PWM_EN  1   // TMR3 三路 PWM (PB0/PB1/PB2)；注意与 UART2 共用 PB1/PB2，不能与 TEST_UART_EN 同开
// ---- UART：软/硬各一份，共用 PB2(TX)/PB1(RX)，一次只开一个 ----
// #define TEST_UART_EN    1        // 硬件 UART2 回环 (跳线 PB2<->PB1)
// #define TEST_UART_SEND_EN  1   // 硬件 UART2 持续发送 (PB2->USB-TTL)
// #define TEST_UART_RECV_EN  1   // 硬件 UART2 持续接收 (USB-TTL->PB1)
// #define TEST_UART_CONSOLE_EN 1  // 硬件 UART2 收发回显 + printf 重定向到 UART2 (PB2/PB1)
// #define TEST_UART_SOFT_EN  1     // 软件 bit-bang UART 回环 (跳线 PB2<->PB1, 9600 8N1)
// ---- SPI：软/硬共用 PE4=CS/PE6=CLK/PE7=MOSI/PE5=MISO，一次只开一个 ----
// ---- SPI：5 个测试，按接法分 3 组（跳线 / LA / W25Q64 Flash），接线互斥 ----
//       LOOP↔WAVE↔W25Q64↔TIMING 不能同开（共用 PE4/PE5/PE6/PE7）
//       一次烧一个看现象
// #define TEST_SPI_LOOP_EN         1  // 跳线接法：软件回环 + 硬件回环（默认；跳线 PE7<->PE5）
// #define TEST_SPI_WAVE_EN         1  // LA 接法：软件波形 + 硬件波形（PE6/PE7 给 LA）
// #define TEST_SPI_W25Q64_EN       1  // Flash 接法：软硬 W25Q64 全套（CS=PE4/CLK=PE6/DI=PE7/DO=PE5）
// #define TEST_SPI_SOFT_ASM_EN     1  // 纯 GPIO：软件 bit-bang C vs ASM 速度对比
// #define TEST_SPI_TIMING_EN       1  // Flash 接法：polling/INT/DMA 时间对比
// ---- I2C：4个测试，PE6=SCL，PE7=SDA ----
// #define TEST_I2C_EN    1
// #define TEST_I2C_LA_EN   1
// #define TEST_I2C_GPIO_EN      1
//#define TEST_I2C_GPIO_LA_EN   1
// ---- ADKEY：原始值 / 三键映射 / 消抖 / 长按 / 连发一次只开一个 ----
 #define TEST_ADKEY_RAW_EN      1
// #define TEST_ADKEY_MAP_EN      1
// #define TEST_ADKEY_DEBOUNCE_EN 1
// #define TEST_ADKEY_LONG_EN     1
//#define TEST_ADKEY_HOLD_EN     1

AT(.com_rodata.exception)
const char str_cpu_error[] = "ERR: %x, EPC: %x\n";

//INST_FETCH ILLEGAL_INST LSU_ERROR
AT(.com_text.exception)
void exception_isr(void)
{
    printf(str_cpu_error, EXCEPTPND, EPC);

    print_r((u8 *)(EPC-32), 64);
    while(1);
}

// 默认 printf 底层输出：轮询把一个字符从 UART0 发出（PB3 @ 1.5Mbps）
AT(.com_text.uart)
void uart_putchar(char ch)
{
    while (!(UART0CON & BIT(8)));   // 等 TXPND(bit8)=1：发送缓冲空，可写下一字节
    UART0DATA = ch;                 // 写 DATA 触发发送（写 DATA 自动清 TXPND）
}

// Timer2 作为 1µs 自由运行计时基准（delay_us/delay_ms/tick_get 的时钟源）
void timer2_init(void)
{
    TMR2CON = 0;                    // 先停止（清 TMREN），准备配置
    TMR2PR = 0xfffffffful;          // 最大周期，几乎不溢出 → 当作 32 位自由计数器
    TMR2CNT = 0;                    // 计数清零
    TMR2CON |= BIT(2) | BIT(0);     // INCSEL=01(计数 tmr_inc=1MHz 上升沿) + TMREN=1(启动)
}

AT(.com_text.timer)
u32 tick_get(void)
{
    return TMR2CNT;
}

AT(.com_text.timer)
bool tick_check_expire(u32 tick, u32 expire_val)
{
    return ((u32)(TMR2CNT - tick) >= expire_val);
}

AT(.com_text.timer)
void delay_us(uint nus)
{
    u32 tick = tick_get();
    u32 tick_out = TICK_1US * nus + 1;
    while (!tick_check_expire(tick, tick_out)) {
    }
}

AT(.com_text.timer)
void delay_ms(uint n)
{
    u32 tick = tick_get();
    u32 tick_out = TICK_1MS * n;
    while (!tick_check_expire(tick, tick_out)) {
    }
}

AT(.com_text.timer)
void delay_5ms(uint n)
{
    u32 tick = tick_get();
    u32 tick_out = TICK_5MS * n;
    while (!tick_check_expire(tick, tick_out)) {
    }
}

void sd_disable(void)
{
    SD0CON = 0;
    CLKGAT0 &= ~BIT(9);                     //close SD0 CLKGATE
    FUNCMCON0 = 0x0f;                       //close SD0 Mapping
}

//Disable USB module
void usb_disable(void)
{
    USBCON0 = BIT(5);                       //USB Disable
    USBCON1 = 0;
    USBCON2 = 0;
    USBCON3 = 0;

    CLKGAT0 &= ~BIT(14);                    //close USB CLKGAT
}

// 把调试 printf 的 UART0 TX 从默认 PA7 改到 PB3（详见 docs/periph_uart.md 与 periph_gpio.md）
void uart0_mapping_sel(void)
{
    //关闭 UART0 默认的 PA7 打印引脚
    GPIOAPU  &= ~BIT(7);                            // 关 PA7 上拉
    GPIOAFEN &= ~BIT(7);                            // FEN=0：PA7 退出功能映射，回到普通 GPIO
    GPIOADIR |= BIT(7);                             // DIR=1：设为输入（不再驱动）
    GPIOADE  &= ~BIT(7);                            // DE=0：改为模拟态，彻底让出
    FUNCMCON0 = (0xf << 12) | (0xf << 8);           // UT0RXMAP/UT0TXMAP=0xF：清除 UART0 旧映射

    //改用 PB3（USBDP 复用脚）做 UART0 TX 打印
    GPIOBDE  |= BIT(3);                             // DE=1：数字 IO
    GPIOBPU  |= BIT(3);                             // 上拉，空闲为高（UART 空闲电平）
    GPIOBDIR |= BIT(3);                             // 方向位（功能映射下由外设接管，此处保留原逻辑）
    GPIOBFEN |= BIT(3);                             // FEN=1：PB3 交给功能映射
    FUNCMCON0 = (7 << 12) | (3 << 8);               // UT0RXMAP=7(由TX决定单线) + UT0TXMAP=3(TX0→G3=PB3)
}

void set_sys_clk(u32 sys_clk)
{
    u32 cpu_ie;
    u32 uart_baud, spll_div = 0, spi_baud = 0;

    if (sys_clk == SYS_24M) {
        spll_div = 1;
        spi_baud = 1;
        uart_baud = (((24000000 + (UART_BAUD / 2)) / UART_BAUD) - 1);
    } else if (sys_clk == SYS_48M) {
        spll_div = 0;
        spi_baud = 3;
        uart_baud = (((48000000 + (UART_BAUD / 2)) / UART_BAUD) - 1);
    } else {
        return;
    }

    cpu_ie = PICCON & BIT(0);
    PICCONCLR = BIT(0);                             //disable IRQ, switch system clock

    if(UART0CON & BIT(0)) {
        while (!(UART0CON & BIT(8)));
    }
    CLKCON0 &= ~(BIT(2) | BIT(3));                  //sysclk sel rc2m
    CLKCON2 &= ~(0x1f << 8);                        //reset spll div

    CLKCON0 |= BIT(30);
    CLKCON1 &= ~(BIT(15) | BIT(16));
    CLKCON1 |= (u32)0 << 16;
    CLKCON1 |= (u32)0 << 15;
    CLKCON0 &= ~(BIT(4) | BIT(5) | BIT(6));
    CLKCON0 |= BIT(4);                              //spll select xosc52m_clk
    CLKCON2 |= (spll_div << 8);
    CLKCON0 |= BIT(3);                              //sysclk sel spll

    UART0BAUD = (uart_baud << 16) | uart_baud;
    SPI0BAUD = spi_baud;
    PICCON |= cpu_ie;
}

int main(void)
{
    WDT_DIS();
    usb_disable();
    sd_disable();
    LVDCON &= ~BIT(30);
    FUNCMCON0 = 0xff000000;                             //close unused UART1, UART2 mapping
    FUNCMCON1 = 0xffffffff;
    CLKCON2 &= 0x00ffffff;                              //清 x26m 分频域高 8 位
    CLKCON2 |= (25 << 24);                              //x26m_div_clk = 26M/26 = 1MHz (timer/ir/fmam 用)
    CLKCON0 &= ~(7 << 23);                              //清 tmr_inc 时钟源选择域
    CLKCON0 |= BIT(24);                                 //tmr_inc select x26m_div_clk = 1MHz（Timer 计数基准）
    timer2_init();                                      //启动 1µs tick（依赖上面的 tmr_inc=1MHz）
    PWRCON0 |= BIT(20);                                 //PMU normal
    RTCCON3 |= BIT(0);                                  //VDDBT enable

    uart0_mapping_sel();
    UART0BAUD = (UART_BAUD_VAL << 16) | UART_BAUD_VAL;
    memset(&__bss_start, 0, (u32)&__bss_size);          //Clear BSS
    set_sys_clk(SYS_CLK);
    timer0_init();

    PICADR = (u32)&__comm_vma;
    PICCON |= 0x10003;                                  //LOW PRIO interrupt enable

    //Below is test code
    /* 恢复初始化时把 printf 重定向回 UART0 (PB3 @ 1.5Mbps, 默认 uart_putchar)。
       想临时切到 UART2 时再调 uart2_console_init() 即可。 */
    my_printf_init(uart_putchar);

    printf("Hello SMART Flash MiniProj\n");

//    printf("test %%d %%i -123: %d %i\n", -123, -123);
//    printf("test %%u 456: %u\n", 456);
//    printf("test %%x %%X 0x12ab: %x %X\n", 0x12ab, 0x12ab);
//
//    printf("test %%ld %%li -12345678: %ld %li\n", -12345678, -12345678);
//    printf("test %%lu 4567890: %lu\n", 4567890);
//    printf("test %%lx %%lX 0xabcd6789: %lx %lX\n", 0xabcd6789, 0xabcd6789);
//
//    printf("test %%4d %%04i -12: %4d %04i\n", -12, -12);
//    printf("test %%-03lu 4567: %-03lu\n", 4567);
//    printf("test %%-8lx %%08lX 0xabcd: %-8lx %08lX\n", 0xabcd, 0xabcd);
//
//    printf("test %%c RT: %c%c\n", 'R', 'T');
//    printf("test %%s: %s\n", "Success");

    // ===== Peripheral test entry (each guarded by its own ifdef, easy to trim) =====
    // Enable with -DTEST_xxx_EN=1 at compile time, default off to avoid changing default behavior
#ifdef TEST_GPIO_EN
    test_gpio_run();
#endif
#ifdef TEST_TIMER_EN
    test_timer_run();
#endif
#ifdef TEST_TIMER_PWM_EN
    test_timer_pwm_run();
#endif
#ifdef TEST_UART_EN
    test_uart_run();
#endif
#ifdef TEST_UART_SEND_EN
    test_uart2_send_run();
#endif
#ifdef TEST_UART_RECV_EN
    test_uart2_recv_run();
#endif
#ifdef TEST_UART_CONSOLE_EN
    test_uart2_console_run();
#endif
#ifdef TEST_UART_SOFT_EN
    test_uart_soft_run();
#endif
// #ifdef TEST_SPI_EN
//     test_spi_run();
// #endif
#ifdef TEST_SPI_LOOP_EN
    test_spi_loop_run();
#endif
#ifdef TEST_SPI_WAVE_EN
    test_spi_wave_run();
#endif
#ifdef TEST_SPI_W25Q64_EN
    test_spi_w25q64_run();
#endif
#ifdef TEST_SPI_SOFT_ASM_EN
    test_spi_soft_asm_run();
#endif
#ifdef TEST_SPI_TIMING_EN
    test_spi_timing_run();
#endif
#ifdef TEST_I2C_EN
    test_i2c_run();
#endif
#ifdef TEST_I2C_LA_EN
    test_i2c_la_run();
#endif
#ifdef TEST_I2C_GPIO_EN
    test_i2c_gpio_run();
#endif
#ifdef TEST_I2C_GPIO_LA_EN
    test_i2c_gpio_la_run();
#endif
#ifdef TEST_ADKEY_RAW_EN
    test_adkey_raw_run();
#endif
#ifdef TEST_ADKEY_MAP_EN
    test_adkey_map_run();
#endif
#ifdef TEST_ADKEY_DEBOUNCE_EN
    test_adkey_debounce_run();
#endif
#ifdef TEST_ADKEY_LONG_EN
    test_adkey_long_run();
#endif
#ifdef TEST_ADKEY_HOLD_EN
    test_adkey_hold_run();
#endif

    while (1);
    return 0;
}
