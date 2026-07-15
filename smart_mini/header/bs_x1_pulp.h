#ifndef _BS_X1_PULP_H_
#define _BS_X1_PULP_H_

#define p_zero      0
#define p_ra        1
#define p_sp        2
#define p_gp        3
#define p_tp        4
#define p_t0        5
#define p_t1        6
#define p_t2        7
#define p_s0        8
#define p_s1        9
#define p_a0        10
#define p_a1        11
#define p_a2        12
#define p_a3        13
#define p_a4        14
#define p_a5        15
#define p_a6        16
#define p_a7        17
#define p_s2        18
#define p_s3        19
#define p_s4        20
#define p_s5        21
#define p_s6        22
#define p_s7        23
#define p_s8        24
#define p_s9        25
#define p_s10       26
#define p_s11       27
#define p_t3        28
#define p_t4        29
#define p_t5        30
#define p_t6        31


#define x_bitrvti(rd, rs, imm)  .long ((0x17 << 27) | (imm << 20)     | (p_##rs << 15) | (0 << 12) | (p_##rd << 7) | 0x57)  //按imm bits来进行位反,立即数
#define x_bitrvtr(rd, rs, rs1)  .long ((0x0b << 28) | (p_##rs1 << 20) | (p_##rs << 15) | (1 << 12) | (p_##rd << 7) | 0x57)  //按rs1 bits来进行位反,寄存器
#define x_swapw(rd, rs)         .long ((0x0b << 28) | (p_##rs << 15) | (2 << 12) | (p_##rd << 7) | 0x57)       //32bit大小端转换
#define x_swaph(rd, rs)         .long ((0x0b << 28) | (p_##rs << 15) | (3 << 12) | (p_##rd << 7) | 0x57)       //16bit大小端转换
#define x_copyh(rd, rs)         .long ((0x0b << 28) | (p_##rs << 15) | (4 << 12) | (p_##rd << 7) | 0x57)       //high 16bit = low 16bit
#define x_getsign(rd, rs)       .long ((0x0b << 28) | (p_##rs << 15) | (5 << 12) | (p_##rd << 7) | 0x57)       //-1 for positive, 1 for negative and 0 for zero

///pulp Post-Incrementing Load & Store Instructions
#define p_lbi(rd, rs1, imm)     .long ((imm << 20) | (p_##rs1 << 15) | (0 << 12) | (p_##rd << 7) | 0x0b)    //rD = Sext(Mem8(rs1)), rs1 += Imm[11:0](有符号数)
#define p_lbui(rd, rs1, imm)    .long ((imm << 20) | (p_##rs1 << 15) | (4 << 12) | (p_##rd << 7) | 0x0b)    //rD = Zext(Mem8(rs1)), rs1 += Imm[11:0]
#define p_lhi(rd, rs1, imm)     .long ((imm << 20) | (p_##rs1 << 15) | (1 << 12) | (p_##rd << 7) | 0x0b)    //rD = Sext(Mem16(rs1)), rs1 += Imm[11:0]
#define p_lhui(rd, rs1, imm)    .long ((imm << 20) | (p_##rs1 << 15) | (5 << 12) | (p_##rd << 7) | 0x0b)    //rD = Zext(Mem16(rs1)), rs1 += Imm[11:0]
#define p_lwi(rd, rs1, imm)     .long ((imm << 20) | (p_##rs1 << 15) | (2 << 12) | (p_##rd << 7) | 0x0b)    //rD = Mem32(rs1), rs1 += Imm[11:0]

#define p_lb(rd, rs1, rs2)      .long ((0x00 << 25) | (p_##rs2 << 20) | (p_##rs1 << 15) | (7 << 12) | (p_##rd << 7) | 0x0b)   //rD = Sext(Mem8(rs1)), rs1 += rs2
#define p_lbu(rd, rs1, rs2)     .long ((0x20 << 25) | (p_##rs2 << 20) | (p_##rs1 << 15) | (7 << 12) | (p_##rd << 7) | 0x0b)   //rD = Zext(Mem8(rs1)), rs1 += rs2
#define p_lh(rd, rs1, rs2)      .long ((0x08 << 25) | (p_##rs2 << 20) | (p_##rs1 << 15) | (7 << 12) | (p_##rd << 7) | 0x0b)   //rD = Sext(Mem16(rs1)), rs1 += rs2
#define p_lhu(rd, rs1, rs2)     .long ((0x28 << 25) | (p_##rs2 << 20) | (p_##rs1 << 15) | (7 << 12) | (p_##rd << 7) | 0x0b)   //rD = Zext(Mem16(rs1)), rs1 += rs2
#define p_lw(rd, rs1, rs2)      .long ((0x10 << 25) | (p_##rs2 << 20) | (p_##rs1 << 15) | (7 << 12) | (p_##rd << 7) | 0x0b)   //rD = Mem32(rs1), rs1 += rs2

#define p_lbrr(rd, rs1, rs2)    .long ((0x00 << 25) | (p_##rs2 << 20) | (p_##rs1 << 15) | (7 << 12) | (p_##rd << 7) | 0x03)   //rD = Sext(Mem8(rs1 + rs2))
#define p_lburr(rd, rs1, rs2)   .long ((0x20 << 25) | (p_##rs2 << 20) | (p_##rs1 << 15) | (7 << 12) | (p_##rd << 7) | 0x03)   //rD = Zext(Mem8(rs1 + rs2))
#define p_lhrr(rd, rs1, rs2)    .long ((0x08 << 25) | (p_##rs2 << 20) | (p_##rs1 << 15) | (7 << 12) | (p_##rd << 7) | 0x03)   //rD = Sext(Mem16(rs1 + rs2))
#define p_lhurr(rd, rs1, rs2)   .long ((0x28 << 25) | (p_##rs2 << 20) | (p_##rs1 << 15) | (7 << 12) | (p_##rd << 7) | 0x03)   //rD = Zext(Mem16(rs1 + rs2))
#define p_lwrr(rd, rs1, rs2)    .long ((0x10 << 25) | (p_##rs2 << 20) | (p_##rs1 << 15) | (7 << 12) | (p_##rd << 7) | 0x03)   //rD = Mem32(rs1 + rs2)

#define p_sbi(rs2, rs1, imm)    .long (((imm>>5) << 25) | (p_##rs2 << 20) | (p_##rs1 << 15) | (0 << 12) | ((imm&0x1f) << 7) | 0x2b) //Mem8(rs1) = rs2, rs1 += Imm[11:0]
#define p_shi(rs2, rs1, imm)    .long (((imm>>5) << 25) | (p_##rs2 << 20) | (p_##rs1 << 15) | (1 << 12) | ((imm&0x1f) << 7) | 0x2b) //Mem16(rs1) = rs2, rs1 += Imm[11:0]
#define p_swi(rs2, rs1, imm)    .long (((imm>>5) << 25) | (p_##rs2 << 20) | (p_##rs1 << 15) | (2 << 12) | ((imm&0x1f) << 7) | 0x2b) //Mem32(rs1) = rs2, rs1 += Imm[11:0]
#define p_sb(rs2, rs1, rs3)     .long ((p_##rs2 << 20) | (p_##rs1 << 15) | (4 << 12) | (p_##rs3 << 7) | 0x2b)   //Mem8(rs1) = rs2, rs1 += rs3
#define p_sh(rs2, rs1, rs3)     .long ((p_##rs2 << 20) | (p_##rs1 << 15) | (5 << 12) | (p_##rs3 << 7) | 0x2b)   //Mem16(rs1) = rs2, rs1 += rs3
#define p_sw(rs2, rs1, rs3)     .long ((p_##rs2 << 20) | (p_##rs1 << 15) | (6 << 12) | (p_##rs3 << 7) | 0x2b)   //Mem32(rs1) = rs2, rs1 += rs3

#define p_sbrr(rs2, rs1, rs3)   .long ((p_##rs2 << 20) | (p_##rs1 << 15) | (4 << 12) | (p_##rs3 << 7) | 0x23)   //Mem8(rs1 + rs3) = rs2
#define p_shrr(rs2, rs1, rs3)   .long ((p_##rs2 << 20) | (p_##rs1 << 15) | (5 << 12) | (p_##rs3 << 7) | 0x23)   //Mem16(rs1 + rs3) = rs2
#define p_swrr(rs2, rs1, rs3)   .long ((p_##rs2 << 20) | (p_##rs1 << 15) | (6 << 12) | (p_##rs3 << 7) | 0x23)   //Mem32(rs1 + rs3) = rs2

///Pulp Bit Manipulation Operations
#define p_bclr(rd, rs1, is3, is2)   .long ((3 << 30) | (is3 << 25) | (is2 << 20) | (p_##rs1 << 15) | (3 << 12) | (p_##rd << 7) | 0x33) //p.bclr rD, rs1, Is3, Is2
#define p_bset(rd, rs1, is3, is2)   .long ((3 << 30) | (is3 << 25) | (is2 << 20) | (p_##rs1 << 15) | (4 << 12) | (p_##rd << 7) | 0x33) //p.bset rD, rs1, Is3, Is2
#define p_ff1(rd, rs1)          .long ((0x08 << 25) | (p_##rs1 << 15) | (0 << 12) | (p_##rd << 7) | 0x33)   //p.ff1 rD, rs1
#define p_fl1(rd, rs1)          .long ((0x08 << 25) | (p_##rs1 << 15) | (1 << 12) | (p_##rd << 7) | 0x33)   //p.fl1 rD, rs1
#define p_clb(rd, rs1)          .long ((0x08 << 25) | (p_##rs1 << 15) | (2 << 12) | (p_##rd << 7) | 0x33)   //p.clb rD, rs1
#define p_cnt(rd, rs1)          .long ((0x08 << 25) | (p_##rs1 << 15) | (3 << 12) | (p_##rd << 7) | 0x33)   //p.cnt rD, rs1
#define p_ror(rd, rs1, rs2)     .long ((0x04 << 25) | (p_##rs2 << 20) | (p_##rs1 << 15) | (5 << 12) | (p_##rd << 7) | 0x33)   //p.ror rD, rs1, rs2

///Pulp General ALU Operations
#define p_abs(rd, rs1)          .long ((0x02 << 25) | (0 << 20)       | (p_##rs1 << 15) | (0 << 12) | (p_##rd << 7) | 0x33) //rD = rs1 < 0 ? –rs1 : rs1
#define p_slet(rd, rs1, rs2)    .long ((0x02 << 25) | (p_##rs2 << 20) | (p_##rs1 << 15) | (2 << 12) | (p_##rd << 7) | 0x33) //rD = rs1 <= rs2 ? 1 : 0 (signed)
#define p_sletu(rd, rs1, rs2)   .long ((0x02 << 25) | (p_##rs2 << 20) | (p_##rs1 << 15) | (3 << 12) | (p_##rd << 7) | 0x33) //rD = rs1 <= rs2 ? 1 : 0 (unsigned)
#define p_min(rd, rs1, rs2)     .long ((0x02 << 25) | (p_##rs2 << 20) | (p_##rs1 << 15) | (4 << 12) | (p_##rd << 7) | 0x33) //rD = rs1 < rs2 ? rs1 : rs2
#define p_minu(rd, rs1, rs2)    .long ((0x02 << 25) | (p_##rs2 << 20) | (p_##rs1 << 15) | (5 << 12) | (p_##rd << 7) | 0x33) //rD = rs1 < rs2 ? rs1 : rs2
#define p_max(rd, rs1, rs2)     .long ((0x02 << 25) | (p_##rs2 << 20) | (p_##rs1 << 15) | (6 << 12) | (p_##rd << 7) | 0x33) //rD = rs1 < rs2 ? rs2 : rs1
#define p_maxu(rd, rs1, rs2)    .long ((0x02 << 25) | (p_##rs2 << 20) | (p_##rs1 << 15) | (7 << 12) | (p_##rd << 7) | 0x33) //rD = rs1 < rs2 ? rs2 : rs1
#define p_exths(rd, rs1)        .long ((0x08 << 25) | (0 << 20)       | (p_##rs1 << 15) | (4 << 12) | (p_##rd << 7) | 0x33) //rD = Sext(rs1[15:0])
#define p_exthz(rd, rs1)        .long ((0x08 << 25) | (0 << 20)       | (p_##rs1 << 15) | (5 << 12) | (p_##rd << 7) | 0x33) //rD = Zext(rs1[15:0])
#define p_extbs(rd, rs1)        .long ((0x08 << 25) | (0 << 20)       | (p_##rs1 << 15) | (6 << 12) | (p_##rd << 7) | 0x33) //rD = Sext(rs1[7:0])
#define p_extbz(rd, rs1)        .long ((0x08 << 25) | (0 << 20)       | (p_##rs1 << 15) | (7 << 12) | (p_##rd << 7) | 0x33) //rD = Zext(rs1[7:0])
#define p_clip(rd, rs1, is2)    .long ((0x0a << 25) | (is2 << 20)     | (p_##rs1 << 15) | (1 << 12) | (p_##rd << 7) | 0x33) //-2^Is2 ~ 2^Is2 – 1
#define p_clipu(rd, rs1, is2)   .long ((0x0a << 25) | (is2 << 20)     | (p_##rs1 << 15) | (2 << 12) | (p_##rd << 7) | 0x33) // 0 ~ 2^Is2-1

#endif
