// GPIO bit-bang I2C logic-analyzer timing test
//
// Pins: PE6 = SCL (manual), PE7 = SDA (manual) - same as hardware I2C
// Same bit-bang primitives as test_i2c_gpio.c
//
// Purpose: emit START + address + STOP bursts so user can measure SCL with LA
// AT24C02 may be disconnected (no slave needed -- bit-bang drives SCL regardless)

#include "test_common.h"

#define PE6_MASK   BIT(6)
#define PE7_MASK   BIT(7)
#define PE6_7_MASK (PE6_MASK | PE7_MASK)

#define SCL_OUT()       do { GPIOEDIR &= ~PE6_MASK; } while (0)  // output mode (CRITICAL: was missing!)
#define SCL_HIGH()      do { SCL_OUT(); GPIOESET = PE6_MASK; } while (0)
#define SCL_LOW()       do { SCL_OUT(); GPIOECLR = PE6_MASK; } while (0)
#define SDA_OUT()       do { GPIOEDIR &= ~PE7_MASK; } while (0)
#define SDA_IN()        do { GPIOEDIR |=  PE7_MASK; } while (0)
#define SDA_OUT_HIGH()  do { SDA_OUT(); GPIOESET = PE7_MASK; } while (0)
#define SDA_OUT_LOW()   do { SDA_OUT(); GPIOECLR = PE7_MASK; } while (0)

#define BURST_COUNT  60

static void la_init_pads(void)
{
    GPIOEDE  |= PE6_7_MASK;
    GPIOEFEN &= ~PE6_7_MASK;
    GPIOEPU  |= PE6_7_MASK;
    GPIOEPD  &= ~PE6_7_MASK;
    SDA_OUT_HIGH();
    SCL_HIGH();
    delay_us(100);
}

static void la_burst(void)
{
    for (u32 i = 0; i < BURST_COUNT; i++) {
        // START
        SDA_OUT_HIGH();
        SCL_HIGH();
        delay_us(5);
        SDA_OUT_LOW();
        delay_us(5);
        SCL_LOW();
        delay_us(5);

        // address byte (0xA0 = 0x50 << 1 | W). Doesn't matter if slave ACKs.
        u8 ctl = (0x50 << 1) | 0;
        for (u8 b = 0; b < 8; b++) {
            if (ctl & 0x80) SDA_OUT_HIGH();
            else             SDA_OUT_LOW();
            delay_us(1);
            SCL_HIGH();  delay_us(5);
            SCL_LOW();   delay_us(5);
            ctl <<= 1;
        }
        // ACK slot (release SDA)
        SDA_IN();
        delay_us(1);
        SCL_HIGH();  delay_us(5);
        SCL_LOW();   delay_us(5);

        // STOP
        SDA_OUT_LOW();
        delay_us(5);
        SCL_HIGH();
        delay_us(5);
        SDA_OUT_HIGH();
        delay_us(5);
    }
}

void test_i2c_gpio_la_run(void)
{
    TEST_LOG("========================================");
    TEST_LOG("GPIO bit-bang I2C - LA timing test");
    TEST_LOG("========================================");
    TEST_LOG("Wire: CH1 = PE6 (SCL), CH2 = PE7 (SDA), GND = GND");
    TEST_LOG("AT24C02 may be disconnected");
    TEST_LOG("========================================");

    la_init_pads();

    TEST_LOG("[Initial state]");
    TEST_LOG("  GPIOEPU    = 0x%08x (should have PE6|PE7 set)", GPIOEPU);
    TEST_LOG("  GPIOEPD    = 0x%08x", GPIOEPD);
    TEST_LOG("  GPIOEDIR   = 0x%08x (PE7=input=release)", GPIOEDIR);
    TEST_LOG("  GPIOE      = 0x%08x (PE6|PE7 should be high)", GPIOE);
    TEST_LOG("");

    TEST_LOG("[Burst 1] emitting %d transactions...", BURST_COUNT);
    TEST_LOG("  >> Trigger LA now on PE6 falling edge <<");
    delay_ms(2000);
    la_burst();
    TEST_LOG("  Burst 1 done.");

    delay_ms(5000);

    TEST_LOG("[Burst 2] verifying repeatability...");
    la_burst();
    TEST_LOG("  Burst 2 done.");

    TEST_LOG("");
    TEST_LOG("========================================");
    TEST_LOG("Measure on PE6:");
    TEST_LOG("  Expected: SCL period ~12 us = ~83 kHz");
    TEST_LOG("  (SCL_low=5us + data_set=1us + SCL_high=5us)");
    TEST_LOG("========================================");

    while (1);
}