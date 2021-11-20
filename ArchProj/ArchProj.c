// ArchProj.c : This file contains the 'main' function. Program execution begins and ends there.

#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ArchProj.h"

#define SHOW_CMD_BREAKDOWN      0
#define SHOW_CONTROL_SIGNALS    0
#define SHOW_CMD                0
#define SHOW_BRANCH             0
#define SHOW_REGS               0
#define SHOW_DUMP               1

#define IMEM_SIZE 1024
#define IMEM_WIDTH 10

int imem_img[IMEM_SIZE];
int imem_size;

int R[15] = { 0 };
int tmp[15] = { 0 };

int count = 0;

State* state;
IF_ID* ifid;
ID_EX* idex;
EX_MEM* exmem;
MEM_WB* memwb;

int branch_taken = 0;   // PCSrc

int hazard = 0;

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
        ifid->pc = idex->addr;
        state->pc = ifid->pc + 1;
        branch_taken = 0;
    }
    else {
        ifid->pc = state->pc;
        state->pc += 1;
    }

    ifid->inst = imem_img[ifid->pc];
    ifid->valid = (ifid->pc < imem_size ? 1 : 0);

    if (SHOW_CMD_BREAKDOWN) {
        if (ifid->valid)
            printf("fetch: %08X\n", ifid->inst);
        else
            printf("fetch: ---\n");
    }

    
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
    exmem->ReadData1 = idex->ReadData1;
    exmem->ReadData2 = idex->ReadData2;
    //printf("%d was: %d %d. ", count, exmem->ReadData1, exmem->ReadData2);

    // NOT SURE WHY LIKE THIS
    //exmem->ReadData1 = (idex->rs == 1 ? idex->imm : R[idex->rs]);
    //exmem->ReadData2 = (idex->rt == 1 ? idex->imm : R[idex->rt]);
    //printf("now: %d %d.\n", exmem->ReadData1, exmem->ReadData2);

    exmem->valid = idex->valid;
    exmem->pc = idex->pc;
    exmem->opcode = idex->ALUOp;
    exmem->RegWrite = idex->RegWrite;
    exmem->rd = idex->rd;
    exmem->rs = idex->rs;
    exmem->rt = idex->rt;

    switch (idex->ALUOp) {
    case ADD:
        if (SHOW_CMD)
            printf("Add\n");
        exmem->result = exmem->ReadData1 + exmem->ReadData2;
        break;
    case SUB:
        exmem->result = exmem->ReadData1 - exmem->ReadData2;
        break;
    case AND:
        exmem->result = exmem->ReadData1 & exmem->ReadData2;
        break;
    case OR:
        exmem->result = exmem->ReadData1 | exmem->ReadData2;
        break;
    case XOR:
        exmem->result = exmem->ReadData1 ^ exmem->ReadData2;
        break;
    case MUL:
        exmem->result = exmem->ReadData1 * exmem->ReadData2;
        break;
    case SLL:
        exmem->result = exmem->ReadData1 << exmem->ReadData2;
        break;
    case SRA:
        exmem->result = exmem->ReadData1 >> exmem->ReadData2;
        break;
    case SRL:
        exmem->result = (int)((unsigned int)exmem->ReadData1 >> exmem->ReadData2);
        break;
    
    case BEQ:
        if (SHOW_BRANCH)
            printf("BEQ %d==%d\n", exmem->ReadData1, exmem->ReadData2);
        if (exmem->ReadData1 == exmem->ReadData2) {
            idex->addr = idex->rd & 0x3FF;
            branch_taken = 1;
        }
        break;
    case BNE:
        if (SHOW_BRANCH)
            printf("BNE %d!=%d\n", exmem->ReadData1, exmem->ReadData2);
        if (exmem->ReadData1 != exmem->ReadData2) {
            idex->addr = idex->rd & 0x3FF;
            branch_taken = 1;
        }
        break;
    case BLT:
        if (SHOW_BRANCH)
            printf("BLT %d<%d\n", exmem->ReadData1, exmem->ReadData2);
        if (exmem->ReadData1 < exmem->ReadData2) {
            idex->addr = idex->rd & 0x3FF;
            branch_taken = 1;
        }
        break;
    case BGT:
        if (SHOW_BRANCH)
            printf("BGT %d>%d\n", exmem->ReadData1, exmem->ReadData2);
        if (exmem->ReadData1 > exmem->ReadData2) {
            idex->addr = idex->rd & 0x3FF;
            branch_taken = 1;
        }
        break;
    case BLE:
        if (SHOW_BRANCH)
            printf("BLE %d<=%d\n", exmem->ReadData1, exmem->ReadData2);
        if (exmem->ReadData1 <= exmem->ReadData2) {
            idex->addr = idex->rd & 0x3FF;
            branch_taken = 1;
        }
        break;
    case BGE:
        if (SHOW_BRANCH)
            printf("BGE %d>=%d\n", exmem->ReadData1, exmem->ReadData2);
        if (exmem->ReadData1 >= exmem->ReadData2) {
            idex->addr = idex->rd & 0x3FF;
            branch_taken = 1;
        }
        break;
    case JAL:
        if (SHOW_BRANCH)
            printf("JAL\n");
        R[15] = state->pc + 1;
        idex->addr = idex->rd & 0x3FF;
        branch_taken = 1;
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
    memwb->rs = exmem->rs;
    memwb->rt = exmem->rt;
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

        // make sure updated values are read
        idex->ReadData1 = (idex->rs == 1 ? idex->imm : R[idex->rs]);
        idex->ReadData2 = (idex->rt == 1 ? idex->imm : R[idex->rt]);
    }
        

}


int hazard_detector() {

    if (idex->valid && exmem->valid) {
        if (exmem->rd == idex->rs && exmem->rd != 0) {
            printf("HAZARD 1a {%d %d}\n", exmem->pc, idex->pc);
            return 3;
        }
        if (exmem->rd == idex->rt && exmem->rd != 0) {
            printf("HAZARD 1b {%d %d}\n", exmem->pc, idex->pc);
            return 3;
        }
    }

    if (idex->valid && memwb->valid) {
        if (memwb->rd == idex->rs && memwb->rd != 0) {
            //printf("HAZARD 2a {%d %d}\n", memwb->pc, idex->pc);
            return 2;
        }
        if (memwb->rd == idex->rt && memwb->rd != 0) {
            printf("HAZARD 2b {%d %d}\n", memwb->pc, idex->pc);
            return 2;
        }
    }
    return 0;
}


int main(int argc, char* argv[]) {
    FILE* f = fopen(argv[1], "r");
    int halt = 0;
    imem_size = load_instruction_memory(f, imem_img, 8);
    init();

    
    int start = 1;
    int halting = 0;
    int halt_prop = 0;

    //while (start || ifid->valid || exmem->valid) {
    while (start || halt_prop < 3) {


        //for (int i = 0; i < 810; i++) {
        start = 0;
        if (halting)
            halt_prop++;

        

        writeback();
        state->W = state->M;
        memory();
        state->M = state->E;

        if (!hazard) {
            execute();
            state->E = state->D;
            decode();
            state->D = state->F;
            fetch();
            state->F = halting ? -1 : ifid->pc;
        }
        // if hazard detected stall
        else {
            state->E = -2;
        }

        if (!hazard)
            hazard = hazard_detector();
        else
            hazard--;


        if (SHOW_DUMP) {
            printf("%3d: ", count);
            char stateF[10];
            char stateD[10];
            char stateE[10];
            char stateM[10];
            char stateW[10];

            if (state->F < 0) {
                strcpy(stateF, "-");
            }
            else {
                _itoa(state->F, stateF, 10);
            }

            if (state->D < 0) {
                strcpy(stateD, "-");
            }
            else {
                _itoa(state->D, stateD, 10);
            }

            if (state->E < 0) {
                strcpy(stateE, "-");
            }
            else {
                _itoa(state->E, stateE, 10);
            }

            if (state->M < 0) {
                strcpy(stateM, "-");
            }
            else {
                _itoa(state->M, stateM, 10);
            }

            if (state->W < 0) {
                strcpy(stateW, "-");
            }
            else {
                _itoa(state->W, stateW, 10);
            }


            printf("%s   %s   %s   %s   %s | ", stateF, stateD, stateE, stateM, stateW);

            for (int j = 2; j < 15; j++) {
                printf("%3X, ", R[j]);
            }
            printf("\n");

        }
        count++;

        //state->F = ((idex->ALUOp == 0x14) ? -1 : ifid->pc);
        if (idex->ALUOp == 0x14) {
            halting = 1;
            state->F = -1;
        }
        else {
            state->F = ifid->pc;
        }

        if (SHOW_REGS) {

            for (int j = 0; j < 15; j++) {
                printf("%3d, ", R[j]);
            }
            printf("\n");
        }




        if (SHOW_CMD || SHOW_CMD_BREAKDOWN || SHOW_CONTROL_SIGNALS || SHOW_REGS)
            printf("------\n");

        //getchar();
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
