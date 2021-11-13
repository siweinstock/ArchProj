#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Each cach has 256 words (lines of 32 bits), and a block is 4 words so offset is 2 bits.
 * There are 64 blocks in a cach so we need 6 bits for index.
 * The rest 12 bits of the adress is tag (the address is 20 bits).
 */

#define BLOCKS_IN_CACH    64
#define WORDS_IN_BLOCK	  4
#define TSRAM_SIZE        64
#define MAIN_MEM_SIZE     1048576


typedef struct DSRAM {
	int core_index;
	// CORE* core;
	int sram[BLOCKS_IN_CACH][WORDS_IN_BLOCK];

} DSRAM;


typedef struct TSRAM {
	int core_index;
	// CORE* core;
	int tags[TSRAM_SIZE];
	int MESI[TSRAM_SIZE];
} TSRAM;


DSRAM* dsram0, * dsram1, * dsram2, * dsram3;

TSRAM* tsram0, * tsram1, * tsram2, * tsram3;


int priorities[4] = { 0, 1, 2, 3 };
int requests[4] = { 0, 0, 0, 0 };

char bus_origid;
char bus_cmd;
char bus_addr;
char bus_data;
char bus_shared;


void init() {
	dsram0 = calloc(1, sizeof(DSRAM));
	dsram0->core_index = 0;
	dsram1 = calloc(1, sizeof(DSRAM));
	dsram1->core_index = 1;
	dsram2 = calloc(1, sizeof(DSRAM));
	dsram2->core_index = 2;
	dsram3 = calloc(1, sizeof(DSRAM));
	dsram3->core_index = 3;
	
	tsram0 = calloc(1, sizeof(TSRAM));
	tsram0->core_index = 0;
	tsram1 = calloc(1, sizeof(TSRAM));
	tsram1->core_index = 1;
	tsram2 = calloc(1, sizeof(TSRAM));
	tsram2->core_index = 2;
	tsram3 = calloc(1, sizeof(TSRAM));
	tsram3->core_index = 3;
}

void core_used_bus(int* priorities_arr, int core_num) {
	int i, j, temp;

	for (i = 0; i < 4; i++) {
		if (priorities_arr[i] == core_num) {
			break;
		}
	}
	for (j = i; j > 0; j--) {
		temp = priorities_arr[j - 1];
		priorities_arr[j - 1] = priorities_arr[j];
		priorities_arr[j] = temp;
	}
}


int choose_core(int* priorities, int* requests) {
	int i, core_num;
	for (i = 3; i >= 0; i--) {
		core_num = priorities[i];
		if (requests[core_num] == 1) {
			requests[core_num] = 0;
			break;
		}
	}
	return core_num;
}

