#pragma once

#define BLOCKS_IN_CACHE      64
#define WORDS_IN_BLOCK	     4
#define MAIN_MEM_SIZE        1048576
#define BLOCKS_IN_MAIN_MEM   262144
#define NO_CMD               0
#define BUSRD                1
#define BUSRDX               2
#define FLUSH                3
#define FLUSH_BUSRD          4
#define FLUSH_BUSRDX         5
#define PRRD                 0
#define PRWR                 1
#define INVALID              0
#define SHARED               1
#define EXCLUSIVE            2
#define MODIFIED             3

typedef struct DSRAM {
	int core_index;
	// CORE* core;
	int sram[BLOCKS_IN_CACHE][WORDS_IN_BLOCK];

} DSRAM;


typedef struct TSRAM {
	int core_index;
	// CORE* core;
	int tags[BLOCKS_IN_CACHE];
	int MESI[BLOCKS_IN_CACHE];
} TSRAM;

typedef struct BUS_REQ {
	int core_index;
	// CORE* core;
	int type; // 1 for BusRd, 2 for BusRdX, 3 for Flush, 4 for Flush+BusRd, 5 for Flush+BusRdX
	int addr;
	int data;
	int offset;
	int index;
	int tag;
	int done;
} BUS_REQ;

typedef struct PR_REQ {
	int core_index;
	// CORE* core;
	int type; // 0 for PrRd, 1 for PrWr
	int addr;
	int data;
	int offset;
	int index;
	int tag;
	int done;
} PR_REQ;

BUS_REQ* requests[4];
int cachestall[4];

void init_caches();
int choose_core();
void bus_step();
void PrRd(PR_REQ* request);