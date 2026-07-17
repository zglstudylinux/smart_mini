#include "include.h"
#include "test/test_gpio.h"
#include "test/test_timer.h"
#include "test/test_uart.h"
#include "test/test_i2c.h"
#include "test/test_i2c_la.h"
#include "test/test_i2c_gpio.h"
#include "test/test_i2c_gpio_la.h"

#define UART_BAUD           1500000
#define UART_BAUD_VAL       (((24000000 + (UART_BAUD / 2)) / UART_BAUD) - 1)

extern u32 __bss_start, __bss_size, __aram_start;
extern u32 __comm_vma, __comm_lma, __comm_size;

// Test entry declarations (each guarded by its own ifdef)
extern void test_gpio_run(void);
extern void test_timer_run(void);
extern void test_uart_run(void);
extern void test_i2c_run(void);
extern void test_i2c_la_run(void);
extern void test_i2c_gpio_run(void);
extern void test_i2c_gpio_la_run(void);

// ===== Test enable switches (enable ONE at a time) =====
// Default: hardware I2C + AT24C02 functional test
// Comment out the default and uncomment one of the others to switch
 #define TEST_GPIO_EN    1
// #define TEST_TIMER_EN   1
// #define TEST_UART_EN    1
// #define TEST_I2C_EN    1
// #define TEST_I2C_LA_EN   1
// #define TEST_I2C_GPIO_EN      1
//#define TEST_I2C_GPIO_LA_EN   1

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

AT(.com_text.uart)
void uart_putchar(char ch)
{
    while (!(UART0CON & BIT(8)));
    UART0DATA = ch;
}

//timer2 for delay function
void timer2_init(void)
{
    TMR2CON = 0;                                            //select tmr_inc rising edge
    TMR2PR = 0xfffffffful;
    TMR2CNT = 0;
    TMR2CON |= BIT(2) | BIT(0);                             //TMR2 Start
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

void uart0_mapping_sel(void)
{
    //close UART0 default PA7 print
    GPIOAPU  &= ~BIT(7);
    GPIOAFEN &= ~BIT(7);                            //Port Function EN
    GPIOADIR |= BIT(7);
    GPIOADE  &= ~BIT(7);
    FUNCMCON0 = (0xf << 12) | (0xf << 8);           //clear uart0 mapping

    //USB PB3 print
    GPIOBDE  |= BIT(3);
    GPIOBPU  |= BIT(3);
    GPIOBDIR |= BIT(3);
    GPIOBFEN |= BIT(3);
    FUNCMCON0 = (7 << 12) | (3 << 8);               //RX0 Map To TX0, TX0 Map to G3
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
    CLKCON2 &= 0x00ffffff;
    CLKCON2 |= (25 << 24);                              //configure x26m_div_clk = 1M (timer, ir, fmam use)
    CLKCON0 &= ~(7 << 23);
    CLKCON0 |= BIT(24);                                 //tmr_inc select x26m_div_clk = 1M
    timer2_init();
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
    printf("Hello SMART Flash MiniProj\n");

    printf("test %%d %%i -123: %d %i\n", -123, -123);
    printf("test %%u 456: %u\n", 456);
    printf("test %%x %%X 0x12ab: %x %X\n", 0x12ab, 0x12ab);

    printf("test %%ld %%li -12345678: %ld %li\n", -12345678, -12345678);
    printf("test %%lu 4567890: %lu\n", 4567890);
    printf("test %%lx %%lX 0xabcd6789: %lx %lX\n", 0xabcd6789, 0xabcd6789);

    printf("test %%4d %%04i -12: %4d %04i\n", -12, -12);
    printf("test %%-03lu 4567: %-03lu\n", 4567);
    printf("test %%-8lx %%08lX 0xabcd: %-8lx %08lX\n", 0xabcd, 0xabcd);

    printf("test %%c RT: %c%c\n", 'R', 'T');
    printf("test %%s: %s\n", "Success");

    // ===== Peripheral test entry (each guarded by its own ifdef, easy to trim) =====
    // Enable with -DTEST_xxx_EN=1 at compile time, default off to avoid changing default behavior
#ifdef TEST_GPIO_EN
    test_gpio_run();
#endif
#ifdef TEST_TIMER_EN
    test_timer_run();
#endif
#ifdef TEST_UART_EN
    test_uart_run();
#endif
// #ifdef TEST_SPI_EN
//     test_spi_run();
// #endif
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

    while (1);
    return 0;
}
