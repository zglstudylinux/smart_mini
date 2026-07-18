#ifndef _TEST_SPI_SOFT_ASM_H_
#define _TEST_SPI_SOFT_ASM_H_

// 软件 bit-bang SPI C vs 内联汇编 速度对比（自 smart_mini_copilot asm_spi_test.c 移植）
// 引脚: PE4=CS, PE5=CLK, PE6=MOSI, PE7=MISO
// 对比 C / ASM循环 / ASM展开 三版 soft_spi_byte 的速度 + W25Q64 JEDEC 功能校验
void test_spi_soft_asm_run(void);

#endif // _TEST_SPI_SOFT_ASM_H_
