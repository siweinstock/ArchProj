// ArchProj.c : This file contains the 'main' function. Program execution begins and ends there.

#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ArchProj.h"

/*#define SHOW_CMD_BREAKDOWN      0
#define SHOW_CONTROL_SIGNALS    0
#define SHOW_CMD                0
#define SHOW_BRANCH             0
#define SHOW_REGS               0
*/
#define SHOW_DUMP               1


#define IMEM_SIZE 1024
#define IMEM_WIDTH 10

int imem_img[4][IMEM_SIZE];
int imem_size[4];

int R[4][16] = { 0 };
int tmp[4][16] = { 0 };

int count[4] = { 0 };

State* state[4];
IF_ID* ifid[4];
ID_EX* idex[4];
EX_MEM* exmem[4];
MEM_WB* memwb[4];

int branch_taken[4] = { 0 };   // PCSrc

int hazard[4] = { 0 };

void init() {
    int i;
    for (i = 0; i < 4; i++) {
        state[i] = calloc(1, sizeof(State));
        state[i]->F = -1;
        state[i]->D = -1;
        state[i]->E = -1;
        state[i]->M = -1;
        state[i]->W = -1;
        idex[i] = calloc(1, sizeof(ID_EX));
        exmem[i] = calloc(1, sizeof(EX_MEM));
        memwb[i] = calloc(1, sizeof(MEM_WB));
        ifid[i] = calloc(1, sizeof(IF_ID));
    }
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

void fetch(int id) {
    if (branch_taken[id]) {
        ifid[id]->pc = idex[id]->addr;
        state[id]->pc = ifid[id]->pc + 1;
        branch_taken[id] = 0;
    }
    else {
        ifid[id]->pc = state[id]->pc;
        state[id]->pc += 1;
    }

    ifid[id]->inst = imem_img[id][ifid[id]->pc];
    ifid[id]->valid = (ifid[id]->pc < imem_size[id] ? 1 : 0);

}

void decode(int id) {
    idex[id]->valid = ifid[id]->valid;
    idex[id]->pc = ifid[id]->pc;

    idex[id]->imm = (ifid[id]->inst & 0xFFF); // sign extended immediate
    idex[id]->rt = (ifid[id]->inst >> 12) & 0xF;
    idex[id]->rs = (ifid[id]->inst >> 16) & 0xF;
    idex[id]->rd = (ifid[id]->inst >> 20) & 0xF;
    idex[id]->ALUOp = (ifid[id]->inst >> 24) & 0xFF;

    idex[id]->RegDst = (idex[id]->rs != 1) && (idex[id]->rt != 1);
    idex[id]->ALUSrc = ((idex[id]->rs == 1 || idex[id]->rt == 1) ? 1 : 0);  // is immediate involved?
    idex[id]->Branch = ((idex[id]->ALUOp >= BEQ && idex[id]->ALUOp <= JAL) ? 1 : 0); // is branch command?
    idex[id]->RegWrite = (((idex[id]->ALUOp >= ADD && idex[id]->ALUOp <= SRL) ||
        (idex[id]->ALUOp >= JAL && idex[id]->ALUOp <= LW)) ? 1 : 0);

    idex[id]->ReadData1 = (idex[id]->rs == 1 ? idex[id]->imm : R[id][idex[id]->rs]);
    idex[id]->ReadData2 = (idex[id]->rt == 1 ? idex[id]->imm : R[id][idex[id]->rt]);

}

void execute(int id) {
    exmem[id]->ReadData1 = idex[id]->ReadData1;
    exmem[id]->ReadData2 = idex[id]->ReadData2;

    exmem[id]->valid = idex[id]->valid;
    exmem[id]->pc = idex[id]->pc;
    exmem[id]->opcode = idex[id]->ALUOp;
    exmem[id]->RegWrite = idex[id]->RegWrite;
    exmem[id]->rd = idex[id]->rd;
    exmem[id]->rs = idex[id]->rs;
    exmem[id]->rt = idex[id]->rt;

    switch (idex[id]->ALUOp) {
    case ADD:
        exmem[id]->result = exmem[id]->ReadData1 + exmem[id]->ReadData2;
        break;
    case SUB:
        exmem[id]->result = exmem[id]->ReadData1 - exmem[id]->ReadData2;
        break;
    case AND:
        exmem[id]->result = exmem[id]->ReadData1 & exmem[id]->ReadData2;
        break;
    case OR:
        exmem[id]->result = exmem[id]->ReadData1 | exmem[id]->ReadData2;
        break;
    case XOR:
        exmem[id]->result = exmem[id]->ReadData1 ^ exmem[id]->ReadData2;
        break;
    case MUL:
        exmem[id]->result = exmem[id]->ReadData1 * exmem[id]->ReadData2;
        break;
    case SLL:
        exmem[id]->result = exmem[id]->ReadData1 << exmem[id]->ReadData2;
        break;
    case SRA:
        exmem[id]->result = exmem[id]->ReadData1 >> exmem[id]->ReadData2;
        break;
    case SRL:
        exmem[id]->result = (int)((unsigned int)exmem[id]->ReadData1 >> exmem[id]->ReadData2);
        break;

    case BEQ:
        if (exmem[id]->ReadData1 == exmem[id]->ReadData2) {
            idex[id]->addr = idex[id]->rd & 0x3FF;
            branch_taken[id] = 1;
        }
        break;
    case BNE:
        if (exmem[id]->ReadData1 != exmem[id]->ReadData2) {
            idex[id]->addr = idex[id]->rd & 0x3FF;
            branch_taken[id] = 1;
        }
        break;
    case BLT:
        if (exmem[id]->ReadData1 < exmem[id]->ReadData2) {
            idex[id]->addr = idex[id]->rd & 0x3FF;
            branch_taken[id] = 1;
        }
        break;
    case BGT:
        if (exmem[id]->ReadData1 > exmem[id]->ReadData2) {
            idex[id]->addr = idex[id]->rd & 0x3FF;
            branch_taken[id] = 1;
        }
        break;
    case BLE:
        if (exmem[id]->ReadData1 <= exmem[id]->ReadData2) {
            idex[id]->addr = idex[id]->rd & 0x3FF;
            branch_taken[id] = 1;
        }
        break;
    case BGE:
        if (exmem[id]->ReadData1 >= exmem[id]->ReadData2) {
            idex[id]->addr = idex[id]->rd & 0x3FF;
            branch_taken[id] = 1;
        }
        break;
    case JAL:
        R[id][15] = state[id]->pc + 1;
        idex[id]->addr = idex[id]->rd & 0x3FF;
        branch_taken[id] = 1;
        break;

    case LW:
        break;
    case SW:
        break;
    case HALT:
        exmem[id]->valid = 0;
        break;
    }
}


void memory(int id) {
    memwb[id]->valid = exmem[id]->valid;
    memwb[id]->pc = exmem[id]->pc;
    memwb[id]->opcode = exmem[id]->opcode;

    memwb[id]->rd = exmem[id]->rd;
    memwb[id]->rs = exmem[id]->rs;
    memwb[id]->rt = exmem[id]->rt;
    memwb[id]->result = exmem[id]->result;

    memwb[id]->result = exmem[id]->result;
    memwb[id]->RegWrite = exmem[id]->RegWrite;

    if (exmem[id]->MemRead) {
        //memwb->ReadData = exmem->result;
        printf("RD=MEM[%X]\n", exmem[id]->result);
    }
    else if (exmem[id]->MemWrite) {
        printf("MEM[%X]=RD\n", exmem[id]->result);
    }

}


void writeback(int id) {
    memcpy(R[id], tmp[id], 16 * sizeof(int));
    if (memwb[id]->RegWrite) {
        tmp[id][memwb[id]->rd] = memwb[id]->result;
        //printf("WB %d\n", state->W);

        // make sure updated values are read
        idex[id]->ReadData1 = (idex[id]->rs == 1 ? idex[id]->imm : R[id][idex[id]->rs]);
        idex[id]->ReadData2 = (idex[id]->rt == 1 ? idex[id]->imm : R[id][idex[id]->rt]);
    }


}


int hazard_detector(int id) {

    if (idex[id]->valid && exmem[id]->valid) {
        if (exmem[id]->rd == idex[id]->rs && exmem[id]->rd != 0) {
            printf("HAZARD 1a {%d %d}\n", exmem[id]->pc, idex[id]->pc);
            return 3;
        }
        if (exmem[id]->rd == idex[id]->rt && exmem[id]->rd != 0) {
            printf("HAZARD 1b {%d %d}\n", exmem[id]->pc, idex[id]->pc);
            return 3;
        }
    }

    if (idex[id]->valid && memwb[id]->valid) {
        if (memwb[id]->rd == idex[id]->rs && memwb[id]->rd != 0) {
            //printf("HAZARD 2a {%d %d}\n", memwb->pc, idex->pc);
            return 2;
        }
        if (memwb[id]->rd == idex[id]->rt && memwb[id]->rd != 0) {
            printf("HAZARD 2b {%d %d}\n", memwb[id]->pc, idex[id]->pc);
            return 2;
        }
    }
    return 0;
}


int main(int argc, char* argv[]) {
    FILE* f[4]; // = fopen(argv[1], "r");
    FILE* fout[4];
    char* nout[4] = { "core0trace.txt", "core1trace.txt", "core2trace.txt", "core3trace.txt" };
    int halt = 0;
    int id;
    //imem_size = load_instruction_memory(f, imem_img, 8);
    for (id = 0; id < 4; id++) {
        f[id] = fopen(argv[id + 1], "r");
        fout[id] = fopen(nout[id], "w");
        imem_size[id] = load_instruction_memory(f[id], imem_img[id], 8);
    }
    init();


    int start = 1;
    int halting[4] = { 0 };
    int halt_prop[4] = { 0 };

    while (start || halt_prop[0] < 3 || halt_prop[1] < 3 || halt_prop[2] < 3 || halt_prop[3] < 3) {
        start = 0;
        for (id = 0; id < 4; id++) {
            if (halt_prop[id] == 3) // core stopped
                continue;
            if (halting[id])
                halt_prop[id]++;

            writeback(id);
            state[id]->W = state[id]->M;
            memory(id);
            state[id]->M = state[id]->E;

            if (!hazard[id]) {
                execute(id);
                state[id]->E = state[id]->D;
                decode(id);
                state[id]->D = state[id]->F;
                fetch(id);
                state[id]->F = halting[id] ? -1 : ifid[id]->pc;
            }
            // if hazard detected stall
            else {
                state[id]->E = -2;
            }

            if (!hazard[id])
                hazard[id] = hazard_detector(id);
            else
                hazard[id]--;

            if (SHOW_DUMP) {
                fprintf(fout[id], "%3d: ", count[id]);
                char stateF[10];
                char stateD[10];
                char stateE[10];
                char stateM[10];
                char stateW[10];

                if (state[id]->F < 0) {
                    strcpy(stateF, "-");
                }
                else {
                    _itoa(state[id]->F, stateF, 10);
                }

                if (state[id]->D < 0) {
                    strcpy(stateD, "-");
                }
                else {
                    _itoa(state[id]->D, stateD, 10);
                }

                if (state[id]->E < 0) {
                    strcpy(stateE, "-");
                }
                else {
                    _itoa(state[id]->E, stateE, 10);
                }

                if (state[id]->M < 0) {
                    strcpy(stateM, "-");
                }
                else {
                    _itoa(state[id]->M, stateM, 10);
                }

                if (state[id]->W < 0) {
                    strcpy(stateW, "-");
                }
                else {
                    _itoa(state[id]->W, stateW, 10);
                }


                fprintf(fout[id], "%s   %s   %s   %s   %s | ", stateF, stateD, stateE, stateM, stateW);

                for (int j = 2; j < 15; j++) {
                    fprintf(fout[id], "%3X, ", R[id][j]);
                }
                fprintf(fout[id], "\n");

            }
            count[id]++;

            if (idex[id]->ALUOp == 0x14) {
                halting[id] = 1;
                state[id]->F = -1;
            }
            else {
                state[id]->F = ifid[id]->pc;
            }

        }

    }

    for (id = 0; id < 4; id++) {
        fclose(fout[id]);
        fclose(f[id]);
    }


    return 0;
}