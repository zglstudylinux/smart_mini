#ifndef _TEST_SPI_TIMING_H_
#define _TEST_SPI_TIMING_H_

// SPI 性能对比测试：polling vs interrupt vs DMA
// 接线：需要 W25Q64 Flash（接 CS=PE4/CLK=PE6/DI=PE7/DO=PE5）
// 不接 Flash 也能跑（无法验证数据，会标 SKIP）
void test_spi_timing_run(void);

#endif // _TEST_SPI_TIMING_H_
