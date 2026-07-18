#ifndef _TEST_SPI_W25Q64_H_
#define _TEST_SPI_W25Q64_H_

// W25Q64 软/硬 SPI 合一测试（合自 test_spi_soft_w25q64 + test_spi_hw_w25q64）
// 接线：接 W25Q64 Flash：CS=PE4/CLK=PE6/DI=PE7/DO=PE5
// 编译宏：
//   SPI_SW_W25_MODE / SPI_HW_W25_MODE 三模式（0/1/2，见 test_spi_w25q64.c 内注释）
//   SPI_W25_RUN_MODE: 0=只软(默认) / 1=只硬 / 2=软硬全跑
//     例：-DSPI_W25_RUN_MODE=2 一键跑完
void test_spi_w25q64_run(void);

#endif // _TEST_SPI_W25Q64_H_
