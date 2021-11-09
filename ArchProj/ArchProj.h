#pragma once
//
// Created by Me on 07/11/2021.
//

#ifndef CORE0_CORE0_H
#define CORE0_CORE0_H

#include <stdint.h>

#define ADD		0
#define SUB		1
#define AND		2
#define OR		3
#define XOR		4
#define MUL		5
#define SLL		6
#define SRA		7
#define SRL		8
#define BEQ		9
#define BNE		10
#define BLT		11
#define BGT		12
#define BLE		13
#define BGE		14
#define JAL		15
#define LW		16
#define SW		17
#define HALT	20


typedef struct IF_ID {
    int pc;
    int inst;
} IF_ID;


typedef struct ID_EX {
    int pc;
    // parsed instruction
    int imm;
    int rt;
    int rs;
    int rd;
    //int opcode;

    // control signals
    int ALUSrc;
    int RegWrite;
    int ALUOp;
    int RegDst;
    int Branch;

} ID_EX;

typedef struct EX_MEM {
    int pc;
    // parsed instruction
    int imm;
    int rt;
    int rs;
    int rd;
    int opcode;
    int result;
    int addr;

    // control signals
    int RegWrite;
    int MemRead;
    int MemWrite;
    int Branch;
    int PCSrc;
    int Zero;
} EX_MEM;

typedef struct MEM_WB {
    int pc;
    // parsed instruction
    int imm;
    int rt;
    int rs;
    int rd;
    int opcode;
    int result;
} MEM_WB;

typedef struct State {
    int pc;
    uint32_t regs[16];
} State;

// struct for holding fragments of parsed operation
typedef struct Operation {
    int opcode;
    int imm;
    int rd;
    int rs;
    int rt;
    int op;
} oper;

#endif //CORE0_CORE0_H
