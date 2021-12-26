// ArchProj.c : This file contains the 'main' function. Program execution begins and ends there.

#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ArchProj.h"
#include "bus.h"

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

//PR_REQ* pr_req[4];

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

// load memory image from file to memory
int load_main_memory(FILE* memfile, int img[]) {
    char line[MAIN_MEM_SIZE];
    int i = 0;
    int n;

    while (fgets(line, 10, memfile)) {
        n = strtol(line, NULL, 16);
        //printf("%d: %s -> %x\n", i, line, n);
        img[i] = n;
        printf("mem[%d]=%x\n", i, n);
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

    exmem[id]->pr_req = NULL;

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
        exmem[id]->MemRead = 1;

        // create requst
        exmem[id]->pr_req = calloc(1, sizeof(PR_REQ));
        exmem[id]->pr_req->type = PRRD;
        exmem[id]->pr_req->addr = exmem[id]->ReadData1 + exmem[id]->ReadData2;
        exmem[id]->pr_req->core_index = id;

        //PrRd(pr_req[id]);
        break;
    case SW:
        exmem[id]->MemWrite = 1;
        // create requst
        exmem[id]->pr_req = calloc(1, sizeof(PR_REQ));
        exmem[id]->pr_req->type = PRWR;
        exmem[id]->pr_req->addr = exmem[id]->ReadData1 + exmem[id]->ReadData2;
        exmem[id]->pr_req->core_index = id;
        exmem[id]->pr_req->data = R[id][exmem[id]->rd];
        printf("store data %x @ address %d\n", R[id][exmem[id]->rd], exmem[id]->pr_req->addr);
        //PrWr(pr_req[id]);
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

    memwb[id]->pr_req = exmem[id]->pr_req;

    // load
    //if (exmem[id]->MemRead) {
    if (exmem[id]->opcode == LW) {
       
        PrRd(memwb[id]->pr_req);
        if (!memwb[id]->pr_req->done) {
        }
        else {
            if (choose_core() == id) {
                printf("cachestall = 1\n");
                cachestall[id] = 1;
            }

            memwb[id]->result = memwb[id]->pr_req->data;
        }


    }
    // store
    //else if (exmem[id]->MemWrite && memwb[id]->pr_req != NULL) {
    if (exmem[id]->opcode == SW) {
        printf("memwb[id]->pr_req->data = %d\n", memwb[id]->pr_req->data);
        PrWr(memwb[id]->pr_req);
        printf("PO put done %d, data %x\n", memwb[id]->pr_req->done, memwb[id]->pr_req->data);
        R[id][memwb[id]->pr_req->addr] = memwb[id]->pr_req->data;

        if (requests[id] == NULL)
            cachestall[id] = 0;
    }
    
}

int writeback(int id) {
    if (memwb[id]->pr_req != NULL && memwb[id]->opcode == LW) { //&& !cachestall[id]) {
        memwb[id]->result = memwb[id]->pr_req->data;
        //printf("memwb res from prreq\n");
        //free(memwb[id]->pr_req);
    }

    memcpy(R[id], tmp[id], 16 * sizeof(int));
    if (memwb[id]->RegWrite) {
        tmp[id][memwb[id]->rd] = memwb[id]->result;

        // make sure updated values are read
        idex[id]->ReadData1 = (idex[id]->rs == 1 ? idex[id]->imm : R[id][idex[id]->rs]);
        idex[id]->ReadData2 = (idex[id]->rt == 1 ? idex[id]->imm : R[id][idex[id]->rt]);
    }



}

int hazard_detector(int id) {

    if (idex[id]->ALUOp == SW && idex[id]->rd == exmem[id]->rd) {
        printf("WAW hazard\n");
        return 3;
    }

    if (idex[id]->valid && exmem[id]->valid) {
        if (id == 0)
            //printf("haz1check: %d %d %d\n", exmem[id]->rd, idex[id]->rs, idex[id]->rt);
        if (exmem[id]->rd == idex[id]->rs && exmem[id]->rd > 1) {
            printf("HAZARD 1a {%d %d}\n", exmem[id]->pc, idex[id]->pc);
            return 3;
        }
        if (exmem[id]->rd == idex[id]->rt && exmem[id]->rd > 1) {
           printf("HAZARD 1b {%d %d}\n", exmem[id]->pc, idex[id]->pc);
            return 3;
        }
    }

    if (idex[id]->valid && memwb[id]->valid) {
        if (id==0)
            //printf("haz2check: %d %d %d\n", memwb[id]->rd, idex[id]->rs, idex[id]->rt);
        if (memwb[id]->rd == idex[id]->rs && memwb[id]->rd > 1) {
            printf("HAZARD 2a {%d %d}\n", memwb[id]->pc, idex[id]->pc);
            return 2;
        }
        if (memwb[id]->rd == idex[id]->rt && memwb[id]->rd > 1) {
            printf("HAZARD 2b {%d %d}\n", memwb[id]->pc, idex[id]->pc);
            return 2;
        }
    }
    return 0;
}


int main(int argc, char* argv[]) {
    FILE* f[5]; // = fopen(argv[1], "r");
    FILE* fout[4];
    char* nout[4] = { "core0trace.txt", "core1trace.txt", "core2trace.txt", "core3trace.txt" };
    int halt = 0;
    int id, core;

    for (id = 0; id < 4; id++) {
        f[id] = fopen(argv[id + 1], "r");
        fout[id] = fopen(nout[id], "w");
        imem_size[id] = load_instruction_memory(f[id], imem_img[id], 8);
    }

    init();
    init_caches();
    f[4] = fopen(argv[5], "r");
    load_main_memory(f[4], main_memory);

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
            core = writeback(id);

            if (!cachestall[id]) {
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

                if (!hazard[id]) {
                    hazard[id] = hazard_detector(id);
                }
                else {
                    hazard[id]--;
                }
            }
            else {
                state[id]->W = -1;
            }


            if (SHOW_DUMP && id==0) {
                fprintf(stdout, "{%d} %3d: ", cachestall[id], count[id]);
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


                fprintf(stdout, "%s   %s   %s   %s   %s | ", stateF, stateD, stateE, stateM, stateW);

                for (int j = 2; j < 15; j++) {
                    fprintf(stdout, "%3X, ", R[id][j]);
                }
                fprintf(stdout, "\n");

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

        bus_step();
        //printf("-\n");
        //getchar();

    }

    for (id = 0; id < 4; id++) {
        fclose(fout[id]);
        fclose(f[id]);
    }


    return 0;
}