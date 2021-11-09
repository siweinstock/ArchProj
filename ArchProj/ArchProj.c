// ArchProj.c : This file contains the 'main' function. Program execution begins and ends there.
//

#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ArchProj.h"

#define IMEM_SIZE 1024
#define IMEM_WIDTH 10

int imem_img[IMEM_SIZE];

State* state;
IF_ID* ifid;
ID_EX* idex;
EX_MEM* exmem;
MEM_WB* memwb;

int branch_taken = 0;   // PCSrc

void init() {
    state = malloc(sizeof(State));
    idex = malloc(sizeof(ID_EX));
    exmem = malloc(sizeof(EX_MEM));
    memwb = malloc(sizeof(MEM_WB));
    ifid = malloc(sizeof(IF_ID));
    memset(ifid, 0, sizeof(IF_ID));
    memset(idex, 0, sizeof(ID_EX));
    memset(exmem, 0, sizeof(EX_MEM));
    memset(memwb, 0, sizeof(MEM_WB));
    memset(state, 0, sizeof(State));
}

// load memory image from file to memory
// mode = number of bits in hex representation (5 or 8)
int load_instruction_memory(FILE* memfile, int img[], int mode) {
    char line[IMEM_WIDTH];
    int i = 0;
    int n;

    while (fgets(line, IMEM_WIDTH, memfile)) {
        n = strtol(line, NULL, 16);
        n = (n << (32 - mode * 4)) >> (32 - mode * 4);	// perform sign extension
        img[i] = n;
        i++;
    }

    return i;
}

void fetch() {
    ifid->pc = (branch_taken ? exmem->addr : state->pc);
    ifid->inst = imem_img[ifid->pc];
}

void decode() {
    idex->pc = ifid->pc;

    idex->imm = (ifid->inst & 0xFFF); // sign extended immediate
    idex->rt = (ifid->inst >> 12) & 0xF;
    idex->rs = (ifid->inst >> 16) & 0xF;
    idex->rd = (ifid->inst >> 20) & 0xF;
    idex->ALUOp = (ifid->inst >> 24) & 0xFF;

    idex->RegDst = (idex->rs != 1) && (idex->rt != 1);
    idex->ALUSrc = ((idex->rs == 1 || idex->rt == 1) ? 1 : 0);  // is immediate involved?
    idex->Branch = ((idex->ALUOp >= BEQ && idex->ALUOp <= JAL) ? 1 : 0); // is branch command?
    idex->RegWrite = (((idex->ALUOp >= ADD && idex->ALUOp <= SRL) ||
        (idex->ALUOp >= JAL && idex->ALUOp <= LW)) ? 1 : 0);

}

void execute() {
    exmem->pc = idex->pc;
    exmem->RegWrite = idex->RegWrite;

    switch (idex->ALUOp) {
    case ADD:
        exmem->result = idex->rs + (idex->RegDst ? idex->imm : idex->rt);
        break;
    case SUB:
        exmem->result = idex->rs - idex->rt;
        break;
    case AND:
        exmem->result = idex->rs & idex->rt;
        break;
    case OR:
        exmem->result = idex->rs | idex->rt;
        break;
    case XOR:
        exmem->result = idex->rs ^ idex->rt;
        break;
    case MUL:
        exmem->result = idex->rs * idex->rt;
        break;
    case SLL:
        exmem->result = idex->rs << idex->rt;
        break;
    case SRA:
        exmem->result = idex->rs >> idex->rt;
        break;
    case SRL:
        exmem->result = (int)((unsigned int)idex->rs >> idex->rt);
        break;
    case BEQ:
        exmem->addr = exmem->pc + idex->imm;
        exmem->result = idex->rs - idex->imm;
        exmem->Zero = (exmem->result == 0 ? 1 : 0);
        branch_taken = (exmem->Zero == 0 ? 1 : 0);
        break;
    case BNE:
        exmem->addr = exmem->pc + idex->imm;
        exmem->result = idex->rs - idex->imm;
        exmem->Zero = (exmem->result == 0 ? 1 : 0);
        branch_taken = (exmem->Zero == 0 ? 0 : 1);
        break;
    case BLT:
        exmem->addr = exmem->pc + idex->imm;
        exmem->result = idex->rs - idex->imm;
        exmem->Zero = (exmem->result == 0 ? 1 : 0);
        branch_taken = (exmem->Zero == 0 ? 0 : 1);
        break;
    case BGT:
        break;
    case BLE:
        break;
    case BGE:
        break;
    case JAL:
        break;
    case LW:
        break;
    case SW:
        break;
    case HALT:
        break;
    }
}

int main(int argc, char* argv[]) {
    FILE* f = fopen(argv[1], "r");
    int halt = 0;
    int imem_size = load_instruction_memory(f, imem_img, 8);

    init();
    fetch();

    return 0;
}


// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file
