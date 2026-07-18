#ifndef _TEST_SPI_COMMON_H_
#define _TEST_SPI_COMMON_H_

// SPI 测试共用定义（供 test_spi_loop/wave/w25q64/asm/timing 共享）
//
// 引脚约定（软/硬复用，硬件 SPI1 G4 默认）：
//   PE4 = CS   (软件做 GPIO 输出，硬件由 GPIO 手动控制)
//   PE6 = CLK  (硬件 SPI1 G4 的 CLK)
//   PE7 = MOSI (硬件 SPI1 G4 的 DO/DI)
//   PE5 = MISO (硬件 SPI1 G4 的 DI/DO)
//
// 软件 bit-bang 重新分配（参考手册 §5.2）：
//   PE4 = CS(GPIO), PE6 = MOSI, PE5 = CLK, PE7 = MISO
//   → 与硬件 SPI 共用 PE4/PE5/PE6/PE7，只是 CLK/MOSI 互换
//   因此跨软/硬时只需切换 GPIO 方向与 FEN，不必重新初始化引脚基本功能

#include "test_common.h"   // 拿到 TEST_LOG、GPIO 宏、ticks 等

#define SPI_CS_PIN    BIT(4)   // PE4
#define SPI_CLK_PIN   BIT(6)   // PE6
#define SPI_MOSI_PIN  BIT(7)   // PE7
#define SPI_MISO_PIN  BIT(5)   // PE5

#define SPI_CS_PORT   E
#define SPI_CLK_PORT  E
#define SPI_MOSI_PORT E
#define SPI_MISO_PORT E

#endif // _TEST_SPI_COMMON_H_
