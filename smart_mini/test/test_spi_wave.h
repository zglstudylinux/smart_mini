#ifndef _TEST_SPI_WAVE_H_
#define _TEST_SPI_WAVE_H_

// SPI 逻辑分析仪波形测试（软件 + 硬件，集成在一个入口）
// 接线：LA 接 PE6(CLK) + PE7(MOSI)；不接跳线不接 Flash
// 编译宏 TEST_SPI_WAVE_PHASE: 0=全跑(默认) / 1=只软波形 / 2=只硬波形
//   如：-DTEST_SPI_WAVE_PHASE=1
void test_spi_wave_run(void);

#endif // _TEST_SPI_WAVE_H_
