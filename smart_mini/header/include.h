#ifndef _INCLUDE_H
#define _INCLUDE_H

#include "global.h"
#include "int.h"

#define TICK_1US    1
#define TICK_1MS    (TICK_1US * 1000)
#define TICK_5MS    (TICK_1MS * 5)

#define SYS_24M     0
#define SYS_48M     1
#define SYS_CLK     SYS_24M

/*****************************************************************************
 * Module    : uart0 Mapping选择列表
 *****************************************************************************/
#define UTX0MAP_PA7     (1 << 8)        //G1 uart0 tx: PA7
#define UTX0MAP_PB2     (2 << 8)        //G2 uart0 tx: PB2
#define UTX0MAP_PB3     (3 << 8)        //G3 uart0 tx: PB3  //USBDP
#define UTX0MAP_PE7     (4 << 8)        //G4 uart0 tx: PE7
#define UTX0MAP_PA1     (5 << 8)        //G5 uart0 tx: PA1
#define UTX0MAP_PE0     (6 << 8)        //G6 uart0 tx: PE0
#define UTX0MAP_PF2     (7 << 8)        //G7 uart0 tx: PF2

#define URX0MAP_PA6     (1 << 12)       //G1 uart0 rx: PA6
#define URX0MAP_PB1     (2 << 12)       //G2 uart0 rx: PB1
#define URX0MAP_PB4     (3 << 12)       //G3 uart0 rx: PB4
#define URX0MAP_PE6     (4 << 12)       //G4 uart0 rx: PE6
#define URX0MAP_PA0     (5 << 12)       //G5 uart0 rx: PA0
#define URX0MAP_PE1     (6 << 12)       //G6 uart0 rx: PE1
#define URX0MAP_TX      (7 << 12)       //G7 uart0 map to TX pin by UT0TXMAP select(1线模式)

#endif
