// AT24C02 hardware I2C functional test
//
// Manual references:
//   [BT892X_UserManual_Driver.md Sec.8 IIC]
//   [bt892x_pinfunction.md Sec.4.3 PE6/PE7] + [Sec.8.5 IIC]
//   [header/sfr.h lines 359-362 IICCON0/1/CMDA/DATA]
//
// Pin assignments (from bt892x_pinfunction.md Sec.4.3 PE6/PE7 + Sec.8.5 IIC):
//   PE6 = IIC_CLK-G5  -> SCL    (pinfunction PE6 row IIC column: IIC_CLK-G5/G6)
//   PE7 = IIC_DAT-G5  -> SDA    (pinfunction PE7 row IIC column: IIC_DAT-G5)
//   G5 is the only Group that simultaneously maps PE6 SCL and PE7 SDA.
//
// IIC clock config (manual Sec.8.1 + Sec.8.2):
//   Sec.8.1 features: "supports async clock sources (RC2M or XOSC26M)"
//   Sec.8.2 formula: SCL = source_clk / ((preclkdiv+1) * (posdiv+1))
//   POSDIV bit 9:4 (manual Sec.8.2 table)
//   HOLDCNT bit 3:2 (manual Sec.8.2 table)
//
// IIC clock gate:
//   CLKGAT2[0] = IIC  source: user-provided CLKGAT register definition table
//                   (table not present in BT892X_UserManual_Driver.md, user confirmed by screenshot)

#include "test_common.h"

// ========== Register bit definitions (manual Sec.8.2 table) ==========
#define IIC_EN         BIT(0)       // IICON0[0]    - IIC main enable
#define IIC_INTEN      BIT(1)       // IICON0[1]    - interrupt enable (off)
#define IIC_CLR_ALL    BIT(27)      // IICON0[27]   - clear all state (W)
#define IIC_KS         BIT(28)      // IICON0[28]   - kick start (W)
#define IIC_CLR_DONE   BIT(29)      // IICON0[29]   - clear DONE (W)
#define IIC_ACKSTATUS  BIT(30)      // IICON0[30]   - 0=ACK, 1=NAK (R)
#define IIC_DONE       BIT(31)      // IICON0[31]   - transfer complete (R)

#define IIC_START0_EN  BIT(3)       // IICON1[3]    - start S
#define IIC_CTL0_EN    BIT(4)       // IICON1[4]    - send CTL0 (addr+R/W)
#define IIC_ADR0_EN    BIT(5)       // IICON1[5]    - send ADR0 (sub-addr)
#define IIC_ADR1_EN    BIT(6)       // IICON1[6]    - send ADR1 (alt sub-addr)
#define IIC_START1_EN  BIT(7)       // IICON1[7]    - repeated start Sr
#define IIC_CTL1_EN    BIT(8)       // IICON1[8]    - send CTL1 (second addr)
#define IIC_RDAT_EN    BIT(9)       // IICON1[9]    - receive data
#define IIC_WDAT_EN    BIT(10)      // IICON1[10]   - send data
#define IIC_STOP_EN    BIT(11)      // IICON1[11]   - send STOP
#define IIC_TXNAK_EN   BIT(12)      // IICON1[12]   - send NAK on last byte

// ========== Pin / Slave constants ==========
#define PE6_MASK       BIT(6)
#define PE7_MASK       BIT(7)
#define PE6_7_MASK     (PE6_MASK | PE7_MASK)

#define AT24C02_ADDR   0x50        // 7-bit address (A0=A1=A2=GND)

// ========== Helper functions ==========

// Wait for IICCON0[DONE] to be set (manual Sec.8.2 IICCON0[31] = DONE)
// timeout_us = 0 means wait forever
static bool test_iic_wait_done(u32 timeout_us)
{
    u32 t0 = TMR2CNT;
    while (!(IICCON0 & IIC_DONE)) {
        if (timeout_us > 0 && (u32)(TMR2CNT - t0) > timeout_us) {
            return false;
        }
    }
    return true;
}

// Initialize IIC controller + PE6/PE7 PAD
// See manual Sec.8.3 step 1~3 for sequence
static void test_i2c_init(void)
{
    // Sec.8.3 step 1: open IIC clock gate (CLKGAT2[0] = IIC)
    CLKGAT2 |= BIT(0);

    // PE6/PE7 PAD: digital IO + pull-up + function select
    GPIOEDE   |=  PE6_7_MASK;   // digital enable
    GPIOEFEN  |=  PE6_7_MASK;   // peripheral function (let IIC own)
    GPIOEPU   |=  PE6_7_MASK;   // internal 10K pull-up (Sec.8.3 requires SDA pull-up)
    GPIOEPD   &= ~PE6_7_MASK;
    GPIOEDIR  &= ~PE6_7_MASK;

    // FUNCMCON2[24:27] = IIC Group G5 (Sec.4.3 + Sec.3.3)
    FUNCMCON2 = (FUNCMCON2 & ~(0xFu << 24)) | (0x5u << 24);

    // Sec.8.3 step 2/3: IICCON0 main config (manual Sec.8.2 table)
    IICCON0 = (0u  << 2)    // HOLDCNT = 0 (manual table)
            | (19u << 4)    // POSDIV = 19
            | IIC_EN;       // IIC_EN = 1 (manual table)
    IICCON0 |= IIC_CLR_ALL;    // manual bit 27
    delay_us(100);
}

// Probe address (START + address + STOP only, no data)
// dev_addr7: 7-bit slave address; is_read: false=write probe, true=read probe
// Returns true=ACK (manual Sec.8.2 ACKSTATUS=0), false=NAK or timeout
static bool test_iic_probe_addr(u8 dev_addr7, bool is_read, u32 timeout_us)
{
    // Sec.8.3 step 4: load command/address (CTL0 = address + R/W)
    IICCMDA = (u8)((dev_addr7 << 1) | (is_read ? 1u : 0u));

    // Sec.8.3 step 6: load action sequence (START + CTL0 + STOP, no data)
    IICCON1 = IIC_START0_EN | IIC_CTL0_EN | IIC_STOP_EN | 0;

    // Sec.8.3 step 7: kick start
    IICCON0 |= IIC_KS;

    if (!test_iic_wait_done(timeout_us)) {
        IICCON0 |= IIC_CLR_DONE;
        return false;
    }
    bool ack = !(IICCON0 & IIC_ACKSTATUS);  // manual Sec.8.2 ACKSTATUS table
    IICCON0 |= IIC_CLR_DONE;               // manual step 9
    return ack;
}

// Write N bytes to AT24C02 sub-address reg_addr (1~4 bytes)
// dev_addr7 + reg_addr (manual Sec.8.2 IICCMDA table)
// Returns true=ACK, false=NAK or timeout
static bool test_iic_write(u8 dev_addr7, u8 reg_addr,
                            const u8 *data, u8 len, u32 timeout_us)
{
    if (len == 0 || len > 4) return false;

    // Sec.8.3 step 4: load IICCMDA (CTL0=address+W, ADR0=sub-address)
    IICCMDA = (u8)((dev_addr7 << 1) & 0xFF)             // CTL0 [7:0]
            | ((u32)reg_addr << 8);                     // ADR0 [15:8]

    // Sec.8.3 step 5: load IICDATA (DATA0 = first byte sent)
    u32 data_word = 0;
    for (u8 i = 0; i < len; i++) {
        data_word |= ((u32)data[i]) << (i * 8);
    }
    IICDATA = data_word;

    // Sec.8.3 step 6: action sequence START + CTL0 + ADR0 + WDAT + STOP
    IICCON1 = IIC_START0_EN | IIC_CTL0_EN | IIC_ADR0_EN
            | IIC_WDAT_EN | IIC_STOP_EN
            | (len & 0x7);                              // DATA_CNT

    IICCON0 |= IIC_KS;

    if (!test_iic_wait_done(timeout_us)) {
        IICCON0 |= IIC_CLR_DONE;
        return false;
    }
    bool ack = !(IICCON0 & IIC_ACKSTATUS);
    IICCON0 |= IIC_CLR_DONE;
    return ack;
}

// Read N bytes from AT24C02 sub-address (with repeated start Sr)
// See manual Sec.8.3 step 6 for START1 + CTL1 + RDAT pattern
static bool test_iic_read(u8 dev_addr7, u8 reg_addr,
                           u8 *buf, u8 len, u32 timeout_us)
{
    if (len == 0 || len > 4) return false;

    // Phase 1: START + address(W) + sub-address (no STOP)
    IICCMDA = (u8)((dev_addr7 << 1) & 0xFF)             // CTL0 = address+W
            | ((u32)reg_addr << 8);                     // ADR0 = sub-address

    // Phase 2: repeated START + address(R)
    // manual Sec.8.2 IICCMDA bit 31:24 = CTL1
    IICCMDA |= (u32)(((dev_addr7 << 1) | 1u) & 0xFF) << 24;

    IICDATA = 0;   // placeholder load

    // Action sequence: START0 + CTL0 + ADR0 + START1 + CTL1 + RDAT + STOP
    // Multi-byte read last byte gets NAK (manual Sec.8.2 IICCON1[12] = TXNAK_EN)
    IICCON1 = IIC_START0_EN | IIC_CTL0_EN | IIC_ADR0_EN
            | IIC_START1_EN | IIC_CTL1_EN
            | IIC_RDAT_EN | IIC_STOP_EN
            | IIC_TXNAK_EN              // last byte NAK (standard I2C)
            | (len & 0x7);

    IICCON0 |= IIC_KS;

    if (!test_iic_wait_done(timeout_us)) {
        IICCON0 |= IIC_CLR_DONE;
        return false;
    }
    // DATA0 = first byte received (manual Sec.8.2 IICDATA table bit 7:0)
    u32 data_word = IICDATA;
    for (u8 i = 0; i < len; i++) {
        buf[i] = (u8)((data_word >> (i * 8)) & 0xFF);
    }
    bool ack = !(IICCON0 & IIC_ACKSTATUS);
    IICCON0 |= IIC_CLR_DONE;
    return ack;
}

// ========== Test entry ==========
void test_i2c_run(void)
{
    TEST_LOG("========================================");
    TEST_LOG("AT24C02 hardware I2C test");
    TEST_LOG("Pins: PE6=SCL, PE7=SDA (Group 5)");
    TEST_LOG("Expect: AT24C02 @ 0x50");
    TEST_LOG("========================================");

    test_i2c_init();

    // ===== Test 1: single address probe =====
    TEST_LOG("[Test 1] Single address probe @ 0x50");
    TEST_LOG("  Watch logic analyzer: PE6=SCL, PE7=SDA");
    delay_ms(2000);   // time for user to connect wires and start LA
    {
        bool ack = test_iic_probe_addr(AT24C02_ADDR, false, 100000);
        TEST_LOG("  Probe 0x50: %s", ack ? "ACK" : "NAK/TIMEOUT");
    }

    delay_ms(500);

    // ===== Test 2: address scan =====
    TEST_LOG("[Test 2] Address scan 0x08..0x77");
    {
        u32 ack_count = 0;
        for (u32 addr = 0x08; addr < 0x78; addr++) {
            if (test_iic_probe_addr((u8)addr, false, 50000)) {
                TEST_LOG("  Found device at 0x%02x", addr);
                ack_count++;
            }
            delay_us(200);
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
        bool ok = test_iic_write(AT24C02_ADDR, 0x00, &val, 1, 100000);
        TEST_LOG("  Write: %s", ok ? "ACK" : "NAK/TIMEOUT");
        if (ok) delay_ms(10);   // AT24C02 write cycle <= 5ms
    }

    delay_ms(500);

    // ===== Test 4: read single byte + compare =====
    TEST_LOG("[Test 4] Read AT24C02 reg 0x00 (expect 0x55)");
    {
        u8 buf[4] = {0};
        bool ok = test_iic_read(AT24C02_ADDR, 0x00, buf, 1, 100000);
        TEST_LOG("  Read: %s, data=0x%02x",
                 ok ? "ACK" : "NAK/TIMEOUT", (u32)buf[0]);
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

        ok = test_iic_write(AT24C02_ADDR, 0x10, wr_buf, 4, 100000);
        TEST_LOG("  Write 0xDE 0xAD 0xBE 0xEF: %s", ok ? "ACK" : "NAK/TIMEOUT");
        if (ok) delay_ms(10);

        ok = test_iic_read(AT24C02_ADDR, 0x10, rd_buf, 4, 100000);
        TEST_LOG("  Read: %s, data=0x%02x 0x%02x 0x%02x 0x%02x",
                 ok ? "ACK" : "NAK/TIMEOUT",
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
    TEST_LOG("AT24C02 test done");
    TEST_LOG("========================================");

    while (1);
}