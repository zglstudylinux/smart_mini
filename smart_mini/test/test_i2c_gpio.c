// AT24C02 GPIO bit-bang I2C functional test
//
// Pin assignments (same as hardware I2C test):
//   PE6 = SCL  (manual GPIO output, software bit-bang)
//   PE7 = SDA  (manual GPIO open-drain emulation via direction switching)
// External AT24C02: VCC=3.3V, GND, SCL<->PE6, SDA<->PE7, A0/A1/A2=GND -> 0x50
//
// Standard I2C timing (100 kHz mode):
//   T_low  >= 4.7 us, T_high >= 4.0 us, T_su_dat >= 250 ns
//   We use: T_low = 5 us, T_high = 5 us, T_su_dat = 1 us
//   => SCL period ~ 12 us per clock = ~83 kHz (close to 100 kHz)
//
// PE6/PE7 is shared with hardware I2C test (hardware I2C also uses PE6=SCL, PE7=SDA).
// But only one test runs at a time, so no conflict.

#include "test_common.h"

#define AT24C02_ADDR   0x50
#define PE6_MASK       BIT(6)       // SCL (shared with hardware IIC, but not used simultaneously)
#define PE7_MASK       BIT(7)       // SDA (shared with hardware IIC, but not used simultaneously)
#define PE6_7_MASK     (PE6_MASK | PE7_MASK)

// PE6 control (SCL is always output)
#define SCL_OUT()       do { GPIOEDIR &= ~PE6_MASK; } while (0)  // output mode (CRITICAL: was missing!)
#define SCL_HIGH()      do { SCL_OUT(); GPIOESET = PE6_MASK; } while (0)  // ensure output then set high
#define SCL_LOW()       do { SCL_OUT(); GPIOECLR = PE6_MASK; } while (0)  // ensure output then set low

// PE7 control (SDA switches between output and input for open-drain emulation)
#define SDA_OUT()       do { GPIOEDIR &= ~PE7_MASK; } while (0)  // output mode
#define SDA_IN()        do { GPIOEDIR |=  PE7_MASK; } while (0)  // input  mode (release, pulled high)
#define SDA_OUT_HIGH()  do { SDA_OUT(); GPIOESET = PE7_MASK; } while (0)
#define SDA_OUT_LOW()   do { SDA_OUT(); GPIOECLR = PE7_MASK; } while (0)
#define SDA_READ()      ((GPIOE & PE7_MASK) ? 1 : 0)

// I2C bit-bang delay macros (~ 100 kHz-ish; for AT24C02 it works at ~83 kHz or even slower)
#define I2C_DELAY()      delay_us(5)   // one half-period (SCL low OR high)
// Helper: data setup time (before SCL rise)
#define I2C_TSU()        delay_us(1)

// ========== I2C primitives (Standard mode, ~80~100 kHz) ==========
// SDA and SCL idle state inline (used implicitly by every transaction)
#define I2C_GPIO_IDLE()  do { SDA_OUT_HIGH(); SCL_HIGH(); delay_us(5); } while (0)

// Send START condition: SDA falls while SCL is high
static void i2c_gpio_start(void)
{
    // bus state should already be: SDA high, SCL high
    SDA_OUT_HIGH();
    SCL_HIGH();
    I2C_DELAY();
    SDA_OUT_LOW();     // SDA falls while SCL high = START
    I2C_DELAY();
    SCL_LOW();         // pull SCL low, prepare for clocking
    I2C_DELAY();
}

// Send STOP condition: SDA rises while SCL is high
static void i2c_gpio_stop(void)
{
    SDA_OUT_LOW();
    I2C_DELAY();
    SCL_HIGH();
    I2C_DELAY();
    SDA_OUT_HIGH();     // SDA rises while SCL high = STOP
    I2C_DELAY();
}

// Write one byte (MSB first), return true if slave ACKed
static bool i2c_gpio_write_byte(u8 data)
{
    for (u8 i = 0; i < 8; i++) {
        // set data
        if (data & 0x80) SDA_OUT_HIGH();
        else             SDA_OUT_LOW();
        I2C_TSU();        // T_su_dat
        SCL_HIGH();
        I2C_DELAY();      // T_high
        SCL_LOW();
        I2C_DELAY();      // T_low
        data <<= 1;
    }
    // ACK slot: release SDA so slave can drive
    SDA_IN();
    I2C_TSU();
    SCL_HIGH();
    I2C_DELAY();
    bool ack = (SDA_READ() == 0);   // 0 = ACK
    SCL_LOW();
    I2C_DELAY();
    return ack;
}

// Read one byte; if send_ack=true, master ACKs; if false, master NAKs (last byte of read)
static u8 i2c_gpio_read_byte(bool send_ack)
{
    u8 val = 0;
    SDA_IN();                // ensure input, release SDA
    for (u8 i = 0; i < 8; i++) {
        SCL_HIGH();
        I2C_DELAY();         // T_high
        val = (val << 1) | SDA_READ();
        SCL_LOW();
        I2C_DELAY();         // T_low
    }
    // Master sends ACK or NAK
    SDA_OUT();
    if (send_ack) SDA_OUT_LOW();
    else          SDA_OUT_HIGH();
    I2C_TSU();
    SCL_HIGH();
    I2C_DELAY();
    SCL_LOW();
    I2C_DELAY();
    SDA_OUT_HIGH();          // release SDA for next bit
    return val;
}

// ========== Higher-level transactions ==========

// Probe address (START + address + STOP). Returns true if ACK.
static bool i2c_gpio_probe_addr(u8 dev_addr7, bool is_read)
{
    i2c_gpio_start();
    u8 ctl = (dev_addr7 << 1) | (is_read ? 1u : 0u);
    bool ack = i2c_gpio_write_byte(ctl);
    i2c_gpio_stop();
    return ack;
}

// Write N bytes to AT24C02 sub-addr reg_addr (1~4 bytes).
// Returns true if all bytes ACKed.
static bool i2c_gpio_write(u8 dev_addr7, u8 reg_addr, const u8 *data, u8 len)
{
    if (len == 0 || len > 4) return false;

    i2c_gpio_start();
    if (!i2c_gpio_write_byte((dev_addr7 << 1) | 0u)) { i2c_gpio_stop(); return false; }  // address+W
    if (!i2c_gpio_write_byte(reg_addr))               { i2c_gpio_stop(); return false; }  // sub-addr
    for (u8 i = 0; i < len; i++) {
        if (!i2c_gpio_write_byte(data[i]))           { i2c_gpio_stop(); return false; }  // data byte
    }
    i2c_gpio_stop();
    return true;
}

// Read N bytes from AT24C02 sub-addr reg_addr (1~4 bytes, with repeated start Sr).
// Returns true if device ACKed.
static bool i2c_gpio_read(u8 dev_addr7, u8 reg_addr, u8 *buf, u8 len)
{
    if (len == 0 || len > 4) return false;

    i2c_gpio_start();
    if (!i2c_gpio_write_byte((dev_addr7 << 1) | 0u)) { i2c_gpio_stop(); return false; }  // address+W
    if (!i2c_gpio_write_byte(reg_addr))               { i2c_gpio_stop(); return false; }  // sub-addr
    i2c_gpio_start();                                                                     // Sr
    if (!i2c_gpio_write_byte((dev_addr7 << 1) | 1u)) { i2c_gpio_stop(); return false; }  // address+R
    for (u8 i = 0; i < len - 1; i++) {
        buf[i] = i2c_gpio_read_byte(true);   // ACK (more bytes coming)
    }
    buf[len - 1] = i2c_gpio_read_byte(false); // NAK (last byte)
    i2c_gpio_stop();
    return true;
}

// ========== GPIO PAD init ==========
static void i2c_gpio_init_pads(void)
{
    // PE6/PE7: digital IO, NO peripheral function (FEN=0 = GPIO mode), pull-up enabled
    GPIOEDE  |= PE6_7_MASK;
    GPIOEFEN &= ~PE6_7_MASK;   // GPIO mode (not peripheral)
    GPIOEPU  |= PE6_7_MASK;    // 10K pull-up (needed for SDA/SCL high idle)
    GPIOEPD  &= ~PE6_7_MASK;
    // Idle bus high
    SDA_OUT_HIGH();
    SCL_HIGH();
    delay_us(100);
}

// ========== Test entry ==========
void test_i2c_gpio_run(void)
{
    TEST_LOG("========================================");
    TEST_LOG("AT24C02 GPIO bit-bang I2C test");
    TEST_LOG("Pins: PE6=SCL, PE7=SDA (manual GPIO)");
    TEST_LOG("Expect: AT24C02 @ 0x50");
    TEST_LOG("========================================");

    i2c_gpio_init_pads();

    // ===== Test 1: single address probe =====
    TEST_LOG("[Test 1] Single address probe @ 0x50");
    TEST_LOG("  Watch logic analyzer: PE6=SCL, PE7=SDA");
    delay_ms(2000);   // time for user to start LA
    {
        bool ack = i2c_gpio_probe_addr(AT24C02_ADDR, false);
        TEST_LOG("  Probe 0x50: %s", ack ? "ACK" : "NAK");
    }

    delay_ms(500);

    // ===== Test 2: address scan =====
    TEST_LOG("[Test 2] Address scan 0x08..0x77");
    {
        u32 ack_count = 0;
        for (u32 addr = 0x08; addr < 0x78; addr++) {
            if (i2c_gpio_probe_addr((u8)addr, false)) {
                TEST_LOG("  Found device at 0x%02x", addr);
                ack_count++;
            }
            delay_us(500);   // bus recovery between probes
        }
        TEST_LOG("  Scan done: %u device(s) found", ack_count);
        if (ack_count == 0) {
            TEST_LOG("  [HINT] No device found - check AT24C02 wiring/pull-ups");
        }
    }

    delay_ms(500);

    // ===== Test 3: write single byte =====
    TEST_LOG("[Test 3] Write 0x55 to AT24C02 reg 0x00");
    {
        u8 val = 0x55;
        bool ok = i2c_gpio_write(AT24C02_ADDR, 0x00, &val, 1);
        TEST_LOG("  Write: %s", ok ? "ACK" : "NAK");
        if (ok) delay_ms(10);   // AT24C02 write cycle
    }

    delay_ms(500);

    // ===== Test 4: read single byte + compare =====
    TEST_LOG("[Test 4] Read AT24C02 reg 0x00 (expect 0x55)");
    {
        u8 buf[4] = {0};
        bool ok = i2c_gpio_read(AT24C02_ADDR, 0x00, buf, 1);
        TEST_LOG("  Read: %s, data=0x%02x",
                 ok ? "ACK" : "NAK", (u32)buf[0]);
        if (ok && buf[0] == 0x55) {
            TEST_LOG("  WRITE-READ PASS");
        } else if (ok) {
            TEST_LOG("  WRITE-READ MISMATCH (got 0x%02x)", (u32)buf[0]);
        }
    }

    delay_ms(500);

    // ===== Test 5: 4-byte pattern =====
    TEST_LOG("[Test 5] Write-read 4 bytes pattern @ reg 0x10");
    {
        u8 wr_buf[4] = {0xDE, 0xAD, 0xBE, 0xEF};
        u8 rd_buf[4] = {0};
        bool ok;

        ok = i2c_gpio_write(AT24C02_ADDR, 0x10, wr_buf, 4);
        TEST_LOG("  Write 0xDE 0xAD 0xBE 0xEF: %s", ok ? "ACK" : "NAK");
        if (ok) delay_ms(10);

        ok = i2c_gpio_read(AT24C02_ADDR, 0x10, rd_buf, 4);
        TEST_LOG("  Read: %s, data=0x%02x 0x%02x 0x%02x 0x%02x",
                 ok ? "ACK" : "NAK",
                 (u32)rd_buf[0], (u32)rd_buf[1],
                 (u32)rd_buf[2], (u32)rd_buf[3]);

        if (ok && rd_buf[0] == 0xDE && rd_buf[1] == 0xAD &&
            rd_buf[2] == 0xBE && rd_buf[3] == 0xEF) {
            TEST_LOG("  PATTERN PASS");
        } else if (ok) {
            TEST_LOG("  PATTERN MISMATCH");
        }
    }

    TEST_LOG("========================================");
    TEST_LOG("GPIO bit-bang I2C test done");
    TEST_LOG("========================================");

    while (1);
}