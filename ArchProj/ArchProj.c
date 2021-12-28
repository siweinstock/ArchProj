// ArchProj.c : This file contains the 'main' function. Program execution begins and ends there.

#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ArchProj.h"

#define SHOW_DUMP               1

#define IMEM_SIZE 1024
#define IMEM_WIDTH 10

// default file names
char* fnames[28] = { "sim.exe", "imem0.txt", "imem1.txt", "imem2.txt", "imem3.txt", "memin.txt", "memout.txt", "rgout0.txt", "regout1.txt", "regout2.txt", "regout3.txt", "core0trace.txt", "core1trace.txt", "core2trace.txt", "core3trace.txt", "bustrace.txt", "dsram0.txt", "dsram1.txt", "dsram2.txt", "dsram3.txt", "tsram0.txt", "tsram1.txt", "tsram2.txt", "tsram3.txt", "stats0.txt", "stats1.txt", "stats2.txt", "stats3.txt" };


int imem_img[4][IMEM_SIZE];
int imem_size[4];

int R[4][16] = { 0 };
int tmp[4][16] = { 0 };

int count[4] = { 0 };
int halt_prop[4] = { 0 };

State* state[4];
IF_ID* ifid[4];
ID_EX* idex[4];
EX_MEM* exmem[4];
MEM_WB* memwb[4];

int branch_taken[4] = { 0 };   // PCSrc

int hazard[4] = { 0 };

// initialize structs
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
        img[i] = n;
        i++;
    }

    return i;
}

// fetch RTL
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

// decode RTL
void decode(int id) {
    idex[id]->valid = ifid[id]->valid;
    idex[id]->pc = ifid[id]->pc;

    // parse command
    idex[id]->imm = (ifid[id]->inst & 0xFFF); // sign extended immediate
    idex[id]->rt = (ifid[id]->inst >> 12) & 0xF;
    idex[id]->rs = (ifid[id]->inst >> 16) & 0xF;
    idex[id]->rd = (ifid[id]->inst >> 20) & 0xF;
    idex[id]->ALUOp = (ifid[id]->inst >> 24) & 0xFF;

    // set control signals
    idex[id]->RegDst = (idex[id]->rs != 1) && (idex[id]->rt != 1);
    idex[id]->ALUSrc = ((idex[id]->rs == 1 || idex[id]->rt == 1) ? 1 : 0);  // is immediate involved?
    idex[id]->Branch = ((idex[id]->ALUOp >= BEQ && idex[id]->ALUOp <= JAL) ? 1 : 0); // is branch command?
    idex[id]->RegWrite = (((idex[id]->ALUOp >= ADD && idex[id]->ALUOp <= SRL) ||
        (idex[id]->ALUOp >= JAL && idex[id]->ALUOp <= LW)) ? 1 : 0);

    idex[id]->ReadData1 = (idex[id]->rs == 1 ? idex[id]->imm : R[id][idex[id]->rs]);
    idex[id]->ReadData2 = (idex[id]->rt == 1 ? idex[id]->imm : R[id][idex[id]->rt]);

}

// execute RTL
void execute(int id) {
    // pass from prev stage
    exmem[id]->ReadData1 = idex[id]->ReadData1;
    exmem[id]->ReadData2 = idex[id]->ReadData2;
    exmem[id]->valid = idex[id]->valid;
    exmem[id]->pc = idex[id]->pc;
    exmem[id]->opcode = idex[id]->ALUOp;
    exmem[id]->RegWrite = idex[id]->RegWrite;
    exmem[id]->rd = idex[id]->rd;
    exmem[id]->rs = idex[id]->rs;
    exmem[id]->rt = idex[id]->rt;

    exmem[id]->pr_req = NULL;   // remove preveous request

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

        break;
    case SW:
        exmem[id]->MemWrite = 1;

        // create requst
        exmem[id]->pr_req = calloc(1, sizeof(PR_REQ));
        exmem[id]->pr_req->type = PRWR;
        exmem[id]->pr_req->addr = exmem[id]->ReadData1 + exmem[id]->ReadData2;
        exmem[id]->pr_req->core_index = id;
        exmem[id]->pr_req->data = R[id][exmem[id]->rd];

        break;
    case HALT:
        exmem[id]->valid = 0;
        break;
    }
}

// memory RTL
void memory(int id) {
    // pass from prev stage
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
    if (exmem[id]->opcode == LW && !memwb[id]->pr_req->done) {
       
        PrRd(memwb[id]->pr_req);
        // cache hit - fetch data from cache
        if (memwb[id]->pr_req->done) {
            memwb[id]->result = memwb[id]->pr_req->data;
        }


    }
    // store
    if (exmem[id]->opcode == SW && !memwb[id]->pr_req->done) {
        PrWr(memwb[id]->pr_req);

        if (requests[id] == NULL)
            cachestall[id] = 0;
    }
    
}

// writeback RTL
int writeback(int id) {
    // if LW is done read correct data
    if (memwb[id]->pr_req != NULL && memwb[id]->opcode == LW && memwb[id]->pr_req->done == 1) {
        memwb[id]->result = memwb[id]->pr_req->data;
    }

    // write to registers with one cycle delay
    memcpy(R[id], tmp[id], 16 * sizeof(int));

    // update registers to be written next cycle
    if (memwb[id]->RegWrite) {
        tmp[id][memwb[id]->rd] = memwb[id]->result;

        // make sure updated values are read
        idex[id]->ReadData1 = (idex[id]->rs == 1 ? idex[id]->imm : R[id][idex[id]->rs]);
        idex[id]->ReadData2 = (idex[id]->rt == 1 ? idex[id]->imm : R[id][idex[id]->rt]);
    }

}

// hazard detection unit. returns number of cycles to stall
int hazard_detector(int id) {
    // WAW detection
    if (idex[id]->ALUOp == SW && idex[id]->rd == exmem[id]->rd) {
        return 3;
    }

    // hazard between decode and execute stages
    if (idex[id]->valid && exmem[id]->valid) {
        if (exmem[id]->rd == idex[id]->rs && exmem[id]->rd > 1) {
            return 3;
        }
        if (exmem[id]->rd == idex[id]->rt && exmem[id]->rd > 1) {
            return 3;
        }
    }
    // hazard between decode and memory stages
    if (idex[id]->valid && memwb[id]->valid) {
        if (memwb[id]->rd == idex[id]->rs && memwb[id]->rd > 1) {
            return 2;
        }
        if (memwb[id]->rd == idex[id]->rt && memwb[id]->rd > 1) {
            return 2;
        }
    }
    return 0;
}

// check if core is halted
int core_stopped(int id) {
    if (halt_prop[id] >= 3 && !cachestall[id] && state[id]->E == -1 && state[id]->M == -1)
        return 1;
    return 0;
}


int main(int argc, char* argv[]) {
    FILE* files[28];

    int halt = 0;
    int id, core;

    // open given files (assume correct) or default if not given
    if (argc == 28) {
        for (int i = 1; i < 28; i++) {
            if (i < 6)
                files[i] = fopen(argv[i], "r");
            else
                files[i] = fopen(argv[i], "w");
        }
    }
    else {
        for (int i = 1; i < 28; i++) {
            if (i < 6)
                files[i] = fopen(fnames[i], "r");
            else
                files[i] = fopen(fnames[i], "w");
        }
    }

    for (id = 0; id < 4; id++) {
        imem_size[id] = load_instruction_memory(files[id + 1], imem_img[id], 8);
    }

    init();
    init_caches();
    load_main_memory(files[5], main_memory);

    int start = 1;  // start signal
    int halting[4] = { 0 }; // track which core is halted

    // keep running as long as at least one core is running (or received start signal)
    while (start || !core_stopped(0) || !core_stopped(1) || !core_stopped(2) || !core_stopped(3)) {
        start = 0;

        for (id = 0; id < 4; id++) {
            
            // core stopped and pipeline cleared
            if (core_stopped(id) && state[id]->D == -1 && state[id]->M == -1 && state[id]->E == -1) {
                continue;
            }
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

            if (SHOW_DUMP) {
                fprintf(files[id+11], "%d ", count[id]);

                char stateF[10];
                char stateD[10];
                char stateE[10];
                char stateM[10];
                char stateW[10];

                if (state[id]->F < 0) {
                    fprintf(files[id + 11], "--- ");
                }
                else {
                    fprintf(files[id + 11], "%03X ", state[id]->F);
                }

                if (state[id]->D < 0) {
                    fprintf(files[id + 11], "--- ");
                }
                else {
                    fprintf(files[id + 11], "%03X ", state[id]->D);
                }

                if (state[id]->E < 0) {
                    fprintf(files[id + 11], "--- ");
                }
                else {
                    fprintf(files[id + 11], "%03X ", state[id]->E);
                }

                if (state[id]->M < 0) {
                    fprintf(files[id + 11], "--- ");
                }
                else {
                    fprintf(files[id + 11], "%03X ", state[id]->M);
                }

                if (state[id]->W < 0) {
                    fprintf(files[id + 11], "--- ");
                }
                else {
                    fprintf(files[id + 11], "%03X ", state[id]->W);
                }

                for (int j = 2; j < 16; j++) {
                    fprintf(files[id + 11], "%08X ", R[id][j]);

                }
                fprintf(files[id + 11], "\n");
   

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
        //getchar();

    }

    for (int i = 1; i < 28; i++) {
        fclose(files[i]);
    }


    return 0;
}