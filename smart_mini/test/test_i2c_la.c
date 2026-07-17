// Hardware I2C timing test (Logic Analyzer dedicated)
//
// Manual references:
//   [BT892X_UserManual_Driver.md Sec.8 IIC]
//   [bt892x_pinfunction.md Sec.4.3 PE6/PE7 + Sec.8.5 IIC]
//   [header/sfr.h lines 359-362 IICCON0/1/CMDA/DATA]
//
// Purpose: when PE6/PE7 are connected to logic analyzer (cannot also have AT24C02),
// run this test. The IIC controller continuously sends START+ADDR+STOP sequences;
// logic analyzer captures SCL waveform. User measures one SCL pulse period.
//
// No AT24C02 dependency: even without a slave, IIC controller drives SCL per config.
// Each transaction = START + 8 data + 1 ACK + STOP = 9 SCL clocks.
//
// Wiring: CH1 -> PE6 (SCL), CH2 -> PE7 (SDA), GND -> GND
// AT24C02 may be disconnected or have VCC off

#include "test_common.h"

#define AT24C02_ADDR   0x50
#define PE6_MASK       BIT(6)
#define PE7_MASK       BIT(7)
#define PE6_7_MASK     (PE6_MASK | PE7_MASK)

#define IIC_EN         BIT(0)
#define IIC_KS         BIT(28)
#define IIC_CLR_ALL    BIT(27)
#define IIC_CLR_DONE   BIT(29)
#define IIC_DONE       BIT(31)
#define IIC_ACKSTATUS  BIT(30)
#define IIC_START0_EN  BIT(3)
#define IIC_CTL0_EN    BIT(4)
#define IIC_STOP_EN    BIT(11)

#define TX_PER_TEST    60     // 60 transactions per test, plenty for LA capture

// IIC init (same config basis as test_i2c.c)
// Manual Sec.8.3 step 1~3
static void la_iic_init(void)
{
    // Sec.8.3 step 1: open IIC clock gate (CLKGAT2[0] = IIC, see CLKGAT table)
    CLKGAT2 |= BIT(0);

    // PE6/PE7 PAD
    GPIOEDE   |=  PE6_7_MASK;
    GPIOEFEN  |=  PE6_7_MASK;
    GPIOEPU   |=  PE6_7_MASK;
    GPIOEPD   &= ~PE6_7_MASK;
    GPIOEDIR  &= ~PE6_7_MASK;

    // FUNCMCON2[24:27] = IIC Group G5 (PE6 SCL + PE7 SDA)
    FUNCMCON2 = (FUNCMCON2 & ~(0xFu << 24)) | (0x5u << 24);

    // Sec.8.3 step 2/3: IICCON0 main config (manual Sec.8.2 table)
    IICCON0 = (0u << 2)    // HOLDCNT = 0
            | (19u << 4)   // POSDIV = 19 (div 20)
            | IIC_EN;      // IIC_EN = 1
    IICCON0 |= IIC_CLR_ALL;
    delay_us(100);
}

// Single IIC transaction (manual Sec.8.3 step 4~9)
// Sends START + address + STOP regardless of ACK
// Note: manual Sec.8.2 IICCON0 bit 31 (DONE) only sets after ACK.
// Without AT24C02, ACK never happens, so timeout protection is essential.
static bool la_probe(u32 timeout_us)
{
    IICCON0 |= IIC_CLR_ALL;

    // Sec.8.3 step 4: IICCMDA = CTL0 = address+W
    IICCMDA = (u8)((AT24C02_ADDR << 1) | 0);

    // Sec.8.3 step 6: action enable = START + CTL0 + STOP, no data
    IICCON1 = IIC_START0_EN | IIC_CTL0_EN | IIC_STOP_EN | 0;

    IICCON0 |= IIC_KS;   // Sec.8.3 step 7: kick start

    // Sec.8.3 step 8: wait DONE, with timeout (no AT24C02 needs timeout)
    u32 t0 = TMR2CNT;
    while (!(IICCON0 & IIC_DONE)) {
        if ((u32)(TMR2CNT - t0) > timeout_us) {
            IICCON0 |= IIC_CLR_DONE;
            return false;
        }
    }
    IICCON0 |= IIC_CLR_DONE;  // Sec.8.3 step 9
    return true;
}

// Run a burst of consecutive transactions
static void la_run_burst(const char *label)
{
    TEST_LOG("[%s] bursting %d transactions...", label, TX_PER_TEST);
    TEST_LOG("  >> Trigger LA now on PE6 rising edge <<");
    delay_ms(2000);    // give LA time to trigger

    u8 ok_count = 0;
    for (u32 i = 0; i < TX_PER_TEST; i++) {
        if (la_probe(1000)) ok_count++;   // 1ms timeout per transaction
    }
    TEST_LOG("  Burst done. ACK=%u/%u (if AT24C02 connected)", ok_count, TX_PER_TEST);
    TEST_LOG("  Measure one SCL pulse on PE6 in LA.");
}

void test_i2c_la_run(void)
{
    TEST_LOG("========================================");
    TEST_LOG("Hardware I2C timing test (Logic Analyzer)");
    TEST_LOG("========================================");
    TEST_LOG("Wire: CH1 = PE6 (SCL), CH2 = PE7 (SDA), GND = GND");
    TEST_LOG("AT24C02 may be disconnected (SCL still drives)");
    TEST_LOG("Manual: Sec.8 IIC, registers IICCON0[POSDIV]/[IIC_EN]");
    TEST_LOG("========================================");

    la_iic_init();

    // Initial state
    TEST_LOG("[Initial state]");
    TEST_LOG("  CLKGAT1 = 0x%08x", CLKGAT1);
    TEST_LOG("  CLKCON1 = 0x%08x", CLKCON1);
    TEST_LOG("  CLKCON2 = 0x%08x", CLKCON2);
    TEST_LOG("  IICCON0 = 0x%08x", IICCON0);
    TEST_LOG("");

    // First burst
    la_run_burst("Burst 1");

    delay_ms(5000);    // 5 second gap

    // Second burst (verify repeatability)
    la_run_burst("Burst 2 (verify)");

    TEST_LOG("");
    TEST_LOG("========================================");
    TEST_LOG("Done.");
    TEST_LOG("Measure SCL single pulse period on PE6:");
    TEST_LOG("  Expected (per Sec.8.2 formula):");
    TEST_LOG("    SCL = IICK / (POSDIV+1) = IICK / 20");
    TEST_LOG("  Your LA reading is the ground-truth actual IICK.");
    TEST_LOG("========================================");

    while (1);
}