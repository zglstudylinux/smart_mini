#ifndef _TEST_SPI_LOOP_H_
#define _TEST_SPI_LOOP_H_

// SPI 跳线回环测试（软回环 + 硬回环，集成在一个入口）
// 接线：跳线 PE7↔PE5；不接 Flash 不接 LA
// 编译宏 TEST_SPI_LOOP_PHASE: 0=全跑(默认) / 1=只软回环 / 2=只硬回环
//   如：-DTEST_SPI_LOOP_PHASE=1
void test_spi_loop_run(void);

#endif // _TEST_SPI_LOOP_H_
