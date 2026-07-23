// AT24C02 hardware I2C functional test —— HAL 重构版
//
// 全部寄存器操作封装在 [i2c_hal.c](i2c_hal.c) 里，本文件只负责
// 跑 5 个测试场景（probe / scan / write 1B / read 1B / 4B 模式）。
//
// 接线：PE6 = SCL (IIC_CLK-G5), PE7 = SDA (IIC_DAT-G5)
// 期望 AT24C02 @ 0x50 (A0=A1=A2=GND)
//
// 手册：BT892X_UserManual_Driver.md §3.2/§3.3/§8.1-§8.3
//       bt892x_pinfunction.md §4.3 PE6/PE7 + §8.5 IIC

#include "test_common.h"
#include "i2c_hal.h"

void test_i2c_run(void)
{
    printf("========================================\n");
    printf("AT24C02 hardware I2C test\n");
    printf("Pins: PE6=SCL, PE7=SDA (Group 5)\n");
    printf("Expect: AT24C02 @ 0x50\n");
    printf("========================================\n");

    i2c_hal_hw_init();

    // ===== Test 1: single address probe =====
    printf("[Test 1] Single address probe @ 0x50\n");
    printf("  Watch logic analyzer: PE6=SCL, PE7=SDA\n");
    delay_ms(2000);
    {
        bool ack = at24c02_hw_probe(AT24C02_ADDR);
        printf("  Probe 0x50: %s\n", ack ? "ACK" : "NAK/TIMEOUT");
    }

    delay_ms(500);

    // ===== Test 2: address scan =====
    printf("[Test 2] Address scan 0x08..0x77\n");
    {
        u32 n = at24c02_hw_scan(0x08, 0x77);
        printf("  Scan done: %u device(s) found\n", n);
        if (n == 0) {
            printf("  [HINT] No device found - check AT24C02 wiring/pull-ups\n");
        }
    }

    delay_ms(500);

    // ===== Test 3: write single byte =====
    printf("[Test 3] Write 0x55 to AT24C02 reg 0x00\n");
    {
        u8 val = 0x55;
        bool ok = at24c02_hw_write_byte(0x00, val);
        printf("  Write: %s\n", ok ? "ACK" : "NAK/TIMEOUT");
    }

    delay_ms(500);

    // ===== Test 4: read single byte + compare =====
    printf("[Test 4] Read AT24C02 reg 0x00 (expect 0x55)\n");
    {
        u8 buf = 0;
        bool ok = at24c02_hw_read_byte(0x00, &buf);
        printf("  Read: %s, data=0x%02x\n", ok ? "ACK" : "NAK/TIMEOUT", (u32)buf);
        if (ok && buf == 0x55) printf("  WRITE-READ PASS\n");
        else if (ok)            printf("  WRITE-READ MISMATCH\n");
    }

    delay_ms(500);

    // ===== Test 5: 4-byte pattern =====
    printf("[Test 5] Write-read 4 bytes pattern @ reg 0x10\n");
    {
        u8 wr_buf[4] = {0xDE, 0xAD, 0xBE, 0xEF};
        u8 rd_buf[4] = {0};

        bool ok = at24c02_hw_write_bytes(0x10, wr_buf, 4);
        printf("  Write 0xDE 0xAD 0xBE 0xEF: %s\n", ok ? "ACK" : "NAK/TIMEOUT");

        ok = at24c02_hw_read_bytes(0x10, rd_buf, 4);
        printf("  Read: %s, data=0x%02x 0x%02x 0x%02x 0x%02x\n",
                 ok ? "ACK" : "NAK/TIMEOUT",
                 (u32)rd_buf[0], (u32)rd_buf[1], (u32)rd_buf[2], (u32)rd_buf[3]);

        if (ok && rd_buf[0] == 0xDE && rd_buf[1] == 0xAD &&
            rd_buf[2] == 0xBE && rd_buf[3] == 0xEF) {
            printf("  PATTERN PASS\n");
        } else if (ok) {
            printf("  PATTERN MISMATCH\n");
        }
    }

    printf("========================================\n");
    printf("AT24C02 test done\n");
    printf("========================================\n");

    while (1);
}