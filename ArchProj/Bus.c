#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Each cach has 256 words (lines of 32 bits), and a block is 4 words so offset is 2 bits.
 * There are 64 blocks in a cach so we need 6 bits for index.
 * The rest 12 bits of the adress is tag (the address is 20 bits).
 */

#define BLOCKS_IN_CACHE    64
#define WORDS_IN_BLOCK	  4
#define MAIN_MEM_SIZE     1048576

int main_memory[MAIN_MEM_SIZE];


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


DSRAM* dsrams_array[4];

TSRAM* tsrams_array[4];


int priorities[4] = { 0, 1, 2, 3 };
int requests[4] = { 0, 0, 0, 0 };

char bus_origid, bus_cmd, bus_addr, bus_data, bus_shared;


void init() {
	dsrams_array[0] = calloc(1, sizeof(DSRAM));
	dsrams_array[0]->core_index = 0;
	dsrams_array[1] = calloc(1, sizeof(DSRAM));
	dsrams_array[1]->core_index = 1;
	dsrams_array[2] = calloc(1, sizeof(DSRAM));
	dsrams_array[2]->core_index = 2;
	dsrams_array[3] = calloc(1, sizeof(DSRAM));
	dsrams_array[3]->core_index = 3;
	
	tsrams_array[0] = calloc(1, sizeof(TSRAM));
	tsrams_array[0]->core_index = 0;
	tsrams_array[1] = calloc(1, sizeof(TSRAM));
	tsrams_array[1]->core_index = 1;
	tsrams_array[2] = calloc(1, sizeof(TSRAM));
	tsrams_array[2]->core_index = 2;
	tsrams_array[3] = calloc(1, sizeof(TSRAM));
	tsrams_array[3]->core_index = 3;
}

//****************** Code for the round robin implementation ************************************************************
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
//*************************************************************************************************************************

void BusRd() {

}

void BusRdX() {

}


void PrRd(int core_index, int addr, int* data) {
	int offset = addr & 0x3;
	int index = (addr >> 2) & 0x3F;
	int tag = (addr >> 8) & 0xFFF;
	TSRAM* tsram = tsrams_array[core_index];
	DSRAM* dsram = dsrams_array[core_index];

	if (tag == tsram->tags[index] && tsram->MESI[index]) { // If there is cache hit and the content is valid we just take the content
		*data = dsram->sram[index][offset];
	}
	else if(tag == tsram->tags[index]){
		BusRd();
	}
	else if (tsram->MESI[index] = 1 || tsram->MESI[index] = 0){
		BusRd();
	}
}


void PrWr(int core_index, int addr, int* data) {
	int offset = addr & 0x3;
	int index = (addr >> 2) & 0x3F;
	int tag = (addr >> 8) & 0xFFF;
	TSRAM* tsram = tsrams_array[core_index];
	DSRAM* dsram = dsrams_array[core_index];

	if (tag == tsram->tags[index] && tsram->MESI[index] == 3) { // If there is cache hit and the state is M we just modify the content
		dsram->sram[index][offset] = *data;
	}
	else if (tag == tsram->tags[index] && tsram->MESI[index] == 2) { // If there is cache hit and the state is E we modify the content and go to state M
		dsram->sram[index][offset] = *data;
		tsram->MESI[index] = 3;
	}
	else if (tag == tsram->tags[index] && tsram->MESI[index] == 1) { // If there is cache hit and the state is S same as above but activate BusRdX
		dsram->sram[index][offset] = *data;
		tsram->MESI[index] = 3;
		BusRdX();
	}
	else if (tag == tsram->tags[index]) { // If there is cache hit and the state is S same as above but activate BusRdX
		tsram->MESI[index] = 3;
		BusRdX();
	}
}

