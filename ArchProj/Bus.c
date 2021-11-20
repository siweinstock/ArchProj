#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Each cach has 256 words (lines of 32 bits), and a block is 4 words so offset is 2 bits.
 * There are 64 blocks in a cach so we need 6 bits for index.
 * The rest 12 bits of the adress is tag (the address is 20 bits).
 * We have 2^20 words in main memory so 2^18 blocks
 */

#define BLOCKS_IN_CACHE      64
#define WORDS_IN_BLOCK	     4
#define MAIN_MEM_SIZE        1048576
#define BLOCKS_IN_MAIN_MEM   262144

// int main_memory[BLOCKS_IN_MAIN_MEM][WORDS_IN_BLOCK];
int main_memory[MAIN_MEM_SIZE];
int main_mem_timer = 0;
int main_mem_is_wating = 0;


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

typedef struct REQS {
	int core_index;
	// CORE* core;
	int type; // 0 for PrRd, 1 for PrWr
	int addr; 
	int* data;
	int offset;
	int index;
	int tag;
} REQS;


DSRAM* dsrams_array[4];

TSRAM* tsrams_array[4];


int priorities[4] = { 0, 1, 2, 3 };
int core_has_request[4] = { 0, 0, 0, 0 };
REQS* requests[4];
REQS* curr_request;

char bus_origid, bus_cmd, bus_addr, bus_data, bus_shared, count_down_to_first_word, request_done, words_to_transfer;


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
void core_used_bus(int core_num) {
	int i, j, temp;

	for (i = 0; i < 4; i++) {
		if (priorities[i] == core_num) {
			break;
		}
	}
	for (j = i; j > 0; j--) {
		temp = priorities[j - 1];
		priorities[j - 1] = priorities[j];
		priorities[j] = temp;
	}
}


int choose_core() {
	int i, core_num;
	for (i = 3; i >= 0; i--) {
		core_num = priorities[i];
		if (core_has_request[core_num] == 1) {
			core_has_request[core_num] = 0;
			return core_num;
		}
	}
	return -1;
}
//*************************************************************************************************************************

void BusRd(int addr, int* data, int core_index) {

}

void BusRdX() {

}


void PrRd(int core_index, int addr, int* data) {
	core_has_request[core_index] = 1;

	requests[core_index] = calloc(1, sizeof(DSRAM));
	requests[core_index]->core_index = core_index;
	requests[core_index]->addr = addr;
	requests[core_index]->type = 0; // indicate PrRd
	requests[core_index]->data = data;
	requests[core_index]->offset = addr & 0x3;
	requests[core_index]->index = (addr >> 2) & 0x3F;
	requests[core_index]->tag = (addr >> 8) & 0xFFF;

	TSRAM* tsram = tsrams_array[core_index];
	DSRAM* dsram = dsrams_array[core_index];

	if (tag == tsram->tags[index]) { // If there is cache hit 
		if (tsram->MESI[index]) {
			*data = dsram->sram[index][offset]; // if the content is valid we just take the content
		}
		else {
			requests[core_index] = calloc(1, sizeof(DSRAM));
			requests[core_index]->core_index = index;
			requests[core_index]->addr = addr;
			requests[core_index]->type = 0; // indicate BusRd
			requests[core_index]->data = data;
		}	
	}
	else { // Cache miss (tricky part)
		if (tsram->MESI[index] == 2 || tsram->MESI[index] == 1 || tsram->MESI[index] == 0) {
			core_has_request[core_index] = 1;
			requests[core_index] = calloc(1, sizeof(DSRAM));
			requests[core_index]->core_index = index;
			requests[core_index]->addr = addr;
			requests[core_index]->type = 0; // indicate BusRd
			requests[core_index]->data = data;
		}
		else if (tsram->MESI[index] == 3) {
			// BusRdX?
		}
	}
	
}


void PrWr(int core_index, int addr, int* data) {
	core_has_request[core_index] = 1;

	requests[core_index] = calloc(1, sizeof(DSRAM));
	requests[core_index]->core_index = core_index;
	requests[core_index]->addr = addr;
	requests[core_index]->type = 1; // indicate PrWr
	requests[core_index]->data = data;
	requests[core_index]->offset = addr & 0x3;
	requests[core_index]->index = (addr >> 2) & 0x3F;
	requests[core_index]->tag = (addr >> 8) & 0xFFF;




	int offset = addr & 0x3;
	int index = (addr >> 2) & 0x3F;
	int tag = (addr >> 8) & 0xFFF;
	TSRAM* tsram = tsrams_array[core_index];
	DSRAM* dsram = dsrams_array[core_index];

	if (tag == tsram->tags[index] && tsram->MESI[index] == 3) { // If there is cache hit and the state is M we just modify the content
		if (tsram->MESI[index] == 3) { // the state is M so we just modify the content
			dsram->sram[index][offset] = *data;
		}
		else if (tsram->MESI[index] == 2) { // state is E so we modify the content and go to state M
			dsram->sram[index][offset] = *data;
			// tsram->MESI[index] = 3;
		}
		else { // the state is S or I so same as above but activate BusRdX
			dsram->sram[index][offset] = *data;
			// tsram->MESI[index] = 3;
			// BusRdX();
		}
	}
	else { // Cache miss

	}

}


void main_mem_snoop() {

	if (bus_cmd == 0) { // Checking if we even have a command in the bus
		return;
	}

	if (bus_cmd < 3) { // bus_cmd is 1 or 2
		main_mem_is_wating = 1;
	}

	if (bus_origid == 4) { // main memory is using the bus
		if (main_mem_timer == 15) { // it is time to flush the content from the memory
			bus_data = main_memory[bus_addr];
		}
	}
}


void core_0_snoop() {
	int offset = bus_addr & 0x3;
	int index = (bus_addr >> 2) & 0x3F;
	int tag = (bus_addr >> 8) & 0xFFF;
	TSRAM* tsram0 = tsrams_array[0];
	DSRAM* dsram0 = dsrams_array[0];
	if (tsram0->tags[index] != tag || bus_cmd == 0) { // Checking if we even have the block in the cache and have a command in the bus
		return;
	}

	if (bus_origid == 0) { // this core is using the bus
		if (bus_cmd == 3) { // flush
			if (bus_addr & 0x3 == 0) { // this means we flushed the whole block already
				bus_cmd = 0;
				tsram0->MESI[index] = 1; // now we share the block and everyone can enjoy it
				return;
			}
		}
		return;
	}
	if (bus_cmd == 1) { // BusRd - need to do something if the block is in state M or E
		if (tsram0->MESI[index] == 3) { // state of the block in this core cache is modified so we need to flush the content
			bus_origid = 0; // core 0
			bus_cmd = 3; // flush
			bus_addr = bus_addr & 0xFFFFC; // the first word in the block

			bus_data = dsram0->sram[index][offset]; // actualy flushing

			tsram0->MESI[index] = 1; // The block is now in shared state
			return;
		}
		else if (tsram0->MESI[index] == 2) {
			tsram0->MESI[index] = 1; // The block is now in shared state
		}
	}
	else if (bus_cmd == 2) { // BusRdX - we invalidate the block anyways and flush if in M mode
		
		if (tsram0->MESI[index] == 3) { // state of the block in this core cache is modified so we need to flush the content
			bus_origid = 0; // core 0
			bus_cmd = 3; // flush
			bus_addr = bus_addr & 0xFFFFC; // the first word in the block
		}
		tsram0->MESI[index] = 0; // we invalidate this block
		return;
	}
	else if (bus_cmd == 3) { // Flush - some other core is flushing
		dsrams_array[0]->sram[index][offset] = bus_data; // we copy the flushed data because it is the most updated
	}
	
}



void state_machine() {
	while (1) {
		if (bus_cmd == 0) {
			// code for seting a request and activating it
		}




		if (count_down_to_first_word) { // means the bus is busy getting the first word in the block
			count_down_to_first_word--;
			continue;
		}
		if (words_to_transfer) { // means there are more words in the block to transfer
			int word_to_transfer = 4 - words_to_transfer;
			int core_index = curr_request->core_index;
			DSRAM* dsram = dsrams_array[core_index];
			int block_beginning_address = (curr_request->tag << 8) | (curr_request->index << 2);
			dsram->sram[curr_request->index][word_to_transfer] = main_memory[block_beginning_address + word_to_transfer]; // the transaction (maybe not good)
			if (word_to_transfer == curr_request->offset) {
				request_done = 1;
				core_has_request[core_index] = 0;
				free(requests[core_index]);
				requests[core_index] = NULL;
			}
			continue;
		}
		int core_to_serve = choose_core();
		core_used_bus(core_to_serve);
		if (core_to_serve == -1) continue; // No transaction needed
		curr_request = requests[core_to_serve];
		int offset = curr_request->offset;
		int index = curr_request->index;
		int tag = curr_request->tag;
		TSRAM* tsram = tsrams_array[core_to_serve];
		DSRAM* dsram = dsrams_array[core_to_serve];

		if (curr_request->type == 0) { // PrRd request
			if (tag == tsram->tags[index]) { // If there is cache hit 
				if (tsram->MESI[index]) { // state is not invalid (S, E or M)
					curr_request->data = dsram->sram[index][offset]; // if the content is valid we just take the content
					request_done = 1;
					continue;
				}
			}

		}
		else {

		}

	}


	while (1) {
		if (bus_cmd == 0) {
			// code for setting a request and activating it
			int core_to_serve = choose_core();
			core_used_bus(core_to_serve);
			if (core_to_serve == -1) continue; // No transaction needed
			curr_request = requests[core_to_serve];
			int offset = curr_request->offset;
			int index = curr_request->index;
			int tag = curr_request->tag;
		}

		core_0_snoop();
		core_1_snoop();
		core_2_snoop();
		core_3_snoop();
		main_mem_snoop();
		if (bus_cmd == 3) {
			advance_adrr();
		}
	}



}

