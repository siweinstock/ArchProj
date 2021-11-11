// ArchProj.c : This file contains the 'main' function. Program execution begins and ends there.

#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ArchProj.h"

#define SHOW_CMD_BREAKDOWN      0
#define SHOW_CONTROL_SIGNALS    0
#define SHOW_CMD                0
#define SHOW_REGS               0
#define SHOW_DUMP               1

#define IMEM_SIZE 1024
#define IMEM_WIDTH 10

int imem_img[IMEM_SIZE];
int imem_size;

int R[15] = { 0 };
int tmp[15] = { 0 };

State* state;
IF_ID* ifid;
ID_EX* idex;
EX_MEM* exmem;
MEM_WB* memwb;

int branch_taken = 0;   // PCSrc

void init() {
    state = calloc(1, sizeof(State));
    state->F = -1;
    state->D = -1;
    state->E = -1;
    state->M = -1;
    state->W = -1;
    idex = calloc(1, sizeof(ID_EX));
    exmem = calloc(1, sizeof(EX_MEM));
    memwb = calloc(1, sizeof(MEM_WB));
    ifid = calloc(1, sizeof(IF_ID));
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
    if (branch_taken) {
        //printf("TAKEN!!!\n");
        ifid->pc = idex->addr;
        state->pc = ifid->pc + 1;
        branch_taken = 0;
    }
    else {
        ifid->pc = state->pc;
        state->pc += 1;
    }

    //printf("PC=%d/%d\n", ifid->pc, imem_size);

    ifid->inst = imem_img[ifid->pc];
    ifid->valid = (ifid->pc < imem_size ? 1 : 0);

    if (SHOW_CMD_BREAKDOWN) {
        if (ifid->valid)
            printf("fetch: %08X\n", ifid->inst);
        else
            printf("fetch: ---\n");
    }

    

    //check_halt();
}

void decode() {
    idex->valid = ifid->valid;
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

    idex->ReadData1 = (idex->rs == 1 ? idex->imm : R[idex->rs]);
    idex->ReadData2 = (idex->rt == 1 ? idex->imm : R[idex->rt]);


    if (SHOW_CMD_BREAKDOWN) {
        if (idex->valid)
            printf("decode: RS=%X, RT=%X, RD=%X, OP=%02X IMM=%03X\n", idex->ReadData1, idex->ReadData2, idex->rd, idex->ALUOp, idex->imm);
        else
            printf("decode: ---\n");
    }

    if (SHOW_CONTROL_SIGNALS && idex->valid)
        printf("decode: RegDst=%d, ALUSrc=%d, Branch=%d, RegWrite=%d\n", idex->RegDst, idex->ALUSrc, idex->Branch, idex->RegWrite);
        

}

void execute() {
    exmem->valid = idex->valid;

    exmem->pc = idex->pc;
    exmem->opcode = idex->ALUOp;
    exmem->RegWrite = idex->RegWrite;
    exmem->rd = idex->rd;


    switch (idex->ALUOp) {
    case ADD:
        if (SHOW_CMD)
            printf("Add\n");
        exmem->result = idex->ReadData1 + idex->ReadData2;
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
        if (exmem->rs == exmem->rt) {
            //exmem->addr = exmem->rd & 0x3FF;
            branch_taken = 1;
        }
        break;
    case BNE:
        if (SHOW_CMD)
            printf("BNE\n");
        break;
    case BLT:
        if (SHOW_CMD)
            printf("BLT\n");
        if (idex->ReadData1 < idex->ReadData2) {
            idex->addr = idex->rd & 0x3FF;
            //printf("BR TAK\n");
            branch_taken = 1;
        }
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
        if (SHOW_CMD)
            printf("HALT\n");
        exmem->valid = 0;

        break;
    }

    if (SHOW_CMD_BREAKDOWN && exmem->valid)
        printf("execute: RS=%X, RT=%X, RD=%X, OP=%02X IMM=%03X\n", idex->rs, idex->rt, idex->rd, idex->ALUOp, idex->imm);
    else if (SHOW_CMD_BREAKDOWN && !exmem->valid)
        printf("execute: ---\n");
}


void memory() {
    memwb->valid = exmem->valid;
    memwb->pc = exmem->pc;
    memwb->opcode = exmem->opcode;

    memwb->rd = exmem->rd;
    memwb->result = exmem->result;

    memwb->result = exmem->result;
    memwb->RegWrite = exmem->RegWrite;

    if (exmem->MemRead) {
        //memwb->ReadData = exmem->result;
        printf("RD=MEM[%X]\n", exmem->result);
    }
    else if (exmem->MemWrite) {
        printf("MEM[%X]=RD\n", exmem->result);
    }

}


void writeback() {
    
    memcpy(R, tmp, 15 * sizeof(int));
    if (memwb->RegWrite) {
        tmp[memwb->rd] = memwb->result;
        //printf("WB %d\n", state->W);
    }
        

}


int main(int argc, char* argv[]) {
    FILE* f = fopen(argv[1], "r");
    int halt = 0;
    imem_size = load_instruction_memory(f, imem_img, 8);
    init();

    int count = 0;
    int start = 1;

    while (start || ifid->valid || exmem->valid) {
    //for (int i = 0; i < 810; i++) {
        start = 0;

        writeback();
        state->W = state->M;
        memory();
        state->M = state->E;
        execute();
        state->E = state->D;
        decode();
        state->D = state->F;
        fetch();
        //state->F = ((ifid->inst & 0xFF000000) >> 24 == 0x14 ? -1 : ifid->pc);
        state->F = (idex->ALUOp == 0x14 ? -1 : ifid->pc);


        if (SHOW_DUMP) {
            printf("%3d: ", count++);
            printf("%3d - %3d - %3d - %3d - %3d | ", state->F, state->D, state->E, state->M, state->W);

            for (int j = 2; j < 15; j++) {
                printf("%3X, ", R[j]);
            }
            printf("\n");

        }

        if (SHOW_REGS) {

            for (int j = 0; j < 15; j++) {
                printf("%3d, ", R[j]);
            }
            printf("\n");
        }




        if (SHOW_CMD || SHOW_CMD_BREAKDOWN || SHOW_CONTROL_SIGNALS || SHOW_REGS)
            printf("------\n");
    }


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
