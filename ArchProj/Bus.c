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
#define NO_CMD               0
#define BUSRD                1
#define BUSRDX               2
#define FLUSH                3
#define FLUSH_BUSRD          4
#define FLUSH_BUSRDX         5
#define PRRD                 0
#define PRWR                 1
#define INVALID              0
#define SHARED                2
#define EXCLUSIVE            3
#define MODIFIED             4



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

typedef struct BUS_REQ {
	int core_index;
	// CORE* core;
	int type; // 1 for BusRd, 2 for BusRdX, 3 for Flush, 4 for Flush+BusRd, 5 for Flush+BusRdX
	int addr; 
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


DSRAM* dsrams_array[4];

TSRAM* tsrams_array[4];


int priorities[4] = { 0, 1, 2, 3 };
int core_has_request[4] = { 0, 0, 0, 0 };
BUS_REQ* requests[4];
PR_REQ* pr_requests[4];
BUS_REQ* curr_request;

char bus_origid, bus_cmd, flush_offset,
bus_shared, count_down_to_first_word, request_done, words_to_transfer, memory_rd, rd_after_flush, rdx_after_flush;

int bus_addr, bus_data;


void init_caches() {
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

void free_caches() {
	free(tsrams_array[0]);
	free(tsrams_array[1]);
	free(tsrams_array[2]);
	free(tsrams_array[3]);
	free(dsrams_array[0]);
	free(dsrams_array[1]);
	free(dsrams_array[2]);
	free(dsrams_array[3]);
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

void BusRd(PR_REQ* request) {

}

void BusRdX() {

}


// This is a function that a core can call in order to request reading data from the cache. 
// when the request is served the field done will be 1 and the requested word will be in field data
void PrRd(PR_REQ* request) {
	int core_index = request->core_index;
	int addr = request->addr;
	int index = request->index;
	int tag = request->tag;
	int offset = request->offset;

	TSRAM* tsram = tsrams_array[core_index];
	DSRAM* dsram = dsrams_array[core_index];

	pr_requests[core_index] = request;

	if (tag == tsram->tags[index]) { // If the block is in the cache 
		if (tsram->MESI[index]) { // The state is all but invalid (not 0, cache hit) 
			request->data = dsram->sram[index][offset]; // we take the content and do not involve the bus
			request->done = 1;
			return;
		}
		else { // The block is invalid (state I)
			// Setting a bus request for reading
			
			requests[core_index] = calloc(1, sizeof(BUS_REQ));
			requests[core_index]->core_index = core_index;
			printf("core_index = %d\n", core_index);
			requests[core_index]->tag = tag;
			requests[core_index]->offset = offset;
			requests[core_index]->addr = addr;
			requests[core_index]->type = BUSRD; // indicate BusRd
			core_has_request[core_index] = 1;
			// Need to check if going to state S or E by snooping!
			return;
		}	
	}
	else { // Cache miss (tricky part)
		if (tsram->MESI[index] != MODIFIED) {
			// BusRd
			requests[core_index] = calloc(1, sizeof(BUS_REQ));
			requests[core_index]->core_index = core_index;
			requests[core_index]->tag = tag;
			requests[core_index]->offset = offset;
			requests[core_index]->addr = addr;
			requests[core_index]->type = BUSRD; // indicate BusRd
			core_has_request[core_index] = 1;
			// Need to check if going to state S or E by snooping!
			return;
		}
		else {
			// Flush + BusRd
			printf("in3");
			requests[core_index] = calloc(1, sizeof(BUS_REQ));
			requests[core_index]->core_index = core_index;
			requests[core_index]->tag = tag;
			requests[core_index]->offset = offset;
			requests[core_index]->addr = addr;
			requests[core_index]->type = FLUSH_BUSRD; // indicate Flush + BusRd
			core_has_request[core_index] = 1;
			// Need to check if going to state S or E by snooping!
			return;
		}
	}
	
}

// This is a function that a core can call in order to request writing data to the cache/memory. 
// when the request is served the field done will be 1
void PrWr(PR_REQ* request) {
	
	int core_index = request->core_index;
	int addr = request->addr;
	int index = request->index;
	int tag = request->tag;
	int offset = request->offset;

	TSRAM* tsram = tsrams_array[core_index];
	DSRAM* dsram = dsrams_array[core_index];

	if (tag == tsram->tags[index]) { // If the block is in the cache
		
		if (tsram->MESI[index] == MODIFIED) { // the state is M so we just modify the content
			dsram->sram[index][offset] = request->data;
			request->done = 1;
			return;
		}
		else if (tsram->MESI[index] == EXCLUSIVE) { // state is E so we modify the content and go to state M
			dsram->sram[index][offset] = request->data;
			tsram->MESI[index] = MODIFIED;
			request->done = 1;
			return;
		}
		else { // the state is S or I so same as above but activate BusRdX
			//dsram->sram[index][offset] = request->data;
			//tsram->MESI[index] = MODIFIED;
			// Setting a bus request for BusRdX
			requests[core_index] = calloc(1, sizeof(BUS_REQ));
			requests[core_index]->core_index = core_index;
			requests[core_index]->tag = tag;
			requests[core_index]->offset = offset;
			requests[core_index]->addr = addr;
			requests[core_index]->type = BUSRDX; // indicate BusRdX
			core_has_request[core_index] = 1;
			// Need to check if going to state S or E by snooping!
			return;
		}
	}
	else { // Cache miss
		if (tsram->MESI[index] == MODIFIED) { // the state is M 
			// Flush + BusRdX
			requests[core_index] = calloc(1, sizeof(BUS_REQ));
			requests[core_index]->core_index = core_index;
			requests[core_index]->tag = tag;
			requests[core_index]->offset = offset;
			requests[core_index]->addr = addr;
			requests[core_index]->type = FLUSH_BUSRDX; // indicate Flush + BusRdX
			core_has_request[core_index] = 1;
			return;
		}
		else {
			// Setting a bus request for BusRdX
			requests[core_index] = calloc(1, sizeof(BUS_REQ));
			requests[core_index]->core_index = core_index;
			requests[core_index]->tag = tag;
			requests[core_index]->offset = offset;
			requests[core_index]->addr = addr;
			requests[core_index]->type = BUSRDX; // indicate BusRdX
			core_has_request[core_index] = 1;
			// Need to check if going to state S or E by snooping!
			return;
		}
	}

}


void main_mem_snoop() {

	if (bus_cmd == NO_CMD || bus_origid == 4) { // Checking if we need to snoop
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


void core_i_snoop(int i) {
	int offset = bus_addr & 0x3;
	int index = (bus_addr >> 2) & 0x3F;
	int tag = (bus_addr >> 8) & 0xFFF;
	TSRAM* tsram = tsrams_array[i];
	DSRAM* dsram = dsrams_array[i];
	if (tsram->tags[index] != tag || bus_cmd == NO_CMD) { // Checking if we even have the block in the cache and have a command in the bus
		return;
	}

	if (bus_origid == i) { // this core is using the bus, no need to do anything because bus logic does it
		return;
	}
	if (bus_cmd == BUSRD) { // BusRd - need to do something if the block is in state M or E
		if (tsram->MESI[index] == MODIFIED) { // state of the block in this core cache is modified so we need to flush the content
			bus_origid = i; // core i
			bus_cmd = FLUSH; // flush
			bus_addr = bus_addr & 0xFFFFC; // the first word in the block

			bus_data = dsram->sram[index][offset]; // actualy flushing

			tsram->MESI[index] = SHARED; // The block is now in shared state

			return;
		}
		else if (tsram->MESI[index] == EXCLUSIVE) {
			tsram->MESI[index] = SHARED; // The block is now in shared state
		}
		bus_shared = 1; // telling the cache that reads that he is not the only one who have this block
	}
	else if (bus_cmd == BUSRDX) { // BusRdX - we invalidate the block anyways and flush if in M mode

		if (tsram->MESI[index] == MODIFIED) { // state of the block in this core cache is modified so we need to flush the content
			bus_origid = i; // core i
			bus_cmd = FLUSH; // flush
			bus_addr = bus_addr & 0xFFFFC; // the first word in the block
		}
		tsram->MESI[index] = INVALID; // we invalidate this block
		return;
	}
	else if (bus_cmd == FLUSH) { // Flush - some other core is flushing
		dsrams_array[i]->sram[index][offset] = bus_data; // we copy the flushed data because it is the most updated
	}
}


void place_request_on_bus() {
	bus_origid = curr_request->core_index;
	int type = curr_request->type;
	bus_addr = curr_request->addr;
	bus_shared = 0;
	
	if (type == BUSRD || type == BUSRDX) {
		bus_cmd = type;
		count_down_to_first_word = 16;
		memory_rd = 1; // Memory needs to respond
	}
	else{
		bus_cmd = FLUSH;
		flush_offset = 0;
		memory_rd = 0; // Memory doesn't need to respond anymore.
		if (type == FLUSH_BUSRD) {
			rd_after_flush = 1;
		}
		else if (type == FLUSH_BUSRDX) {
			rdx_after_flush = 1;
		}
	}
	


}


void bus_logic_before_snooping() {
	int offset = bus_addr & 0x3;
	int index = (bus_addr >> 2) & 0x3F;
	int tag = (bus_addr >> 8) & 0xFFF;
	if (bus_cmd == BUSRD || bus_cmd == BUSRDX) {
		if (!count_down_to_first_word && memory_rd) { // its time for main memory to shine
			
			if (bus_cmd == BUSRD) {
				if (bus_shared) { // we know others have the block
					tsrams_array[bus_origid]->MESI[index] = SHARED;
				}
				else { // only us have it
					tsrams_array[bus_origid]->MESI[index] = EXCLUSIVE;
				}
			}
			
			// activate flush for main memory!!!
			// need to turn off busrd/busrdx!
			bus_cmd = FLUSH;
			bus_origid = 4;
			bus_addr = bus_addr & 0xFFFFFFFC; // the first word in the block
			flush_offset = 0;
			bus_shared = 0;
			memory_rd = 0; // Memory doesn't need to respond anymore.
		}
	}
	if (bus_cmd == FLUSH) {
		if (bus_origid == 4) { // Main memory flush
			bus_data = main_memory[bus_addr]; // flushing to bus
			printf("bus_addr = %d, bus_data = %d\n", bus_addr, bus_data);
		}
		else { // cache is flushing
			bus_data = dsrams_array[bus_origid]->sram[index][offset]; // flushing to bus
			printf("bus_data = %d\n", bus_data);
		}
	}
}


void bus_logic_after_snooping() {
	if (bus_cmd == FLUSH) {
		if (flush_offset == 3) { // This means this was the last word in the block to flush
			bus_cmd = NO_CMD;
			if (bus_origid != 4) { // if it wasnt the main memory flushing
				tsrams_array[bus_origid]->MESI[(bus_addr >> 2) & 0x3F] = SHARED; // now we share the block and everyone can enjoy it
			}
			if (rd_after_flush) {
				// request_Rd();
			}
			else if (rdx_after_flush) {
				// request_RdX();
			}
			else {
				curr_request->done = 1; // request fullfiled
			}
			return;
		}
		else { // continue flushing
			flush_offset++;
			bus_addr++;
		}
		return;
	}
	else if (bus_cmd == BUSRD || bus_cmd == BUSRDX) { // if we got here then none of the caches want to flush it so we continue the countdown (to extinction)
		count_down_to_first_word--;
	}
}


void check_if_req_fulfilled() {
	if (curr_request->done) {
		printf("in\n");
		PR_REQ* pr_req = pr_requests[curr_request->core_index];
		int core_ind = curr_request->core_index;
		// printf("core_ind = \n", core_ind);
		requests[core_ind] = NULL;
		if (pr_req->type == PRRD) { // so core wants to read the updated data
			pr_req->data = dsrams_array[core_ind]->sram[pr_req->index][pr_req->offset]; // giving the core the data from cache
			//printf("pr_req->data = %d, pr_req->index = %d, pr_req->index = %d, pr_req->offset = %d\n", pr_req->data, pr_req->index, pr_req->index, pr_req->offset);
		}
		pr_req->done = 1; // telling the core that it can continue (use the data)
		pr_requests[core_ind] = NULL;
		free(curr_request);
	}
}


void state_machine() {

	while (1) {
		if (bus_cmd == NO_CMD) {
			// code for setting a request and activating it
			int core_to_serve = choose_core();
			if (core_to_serve == -1) continue; // No transaction needed
			curr_request = requests[core_to_serve];
			core_used_bus(core_to_serve); // update the priority
			requests[core_to_serve] = NULL;
			core_has_request[core_to_serve] = 0;
			place_request_on_bus();
		}

		bus_logic_before_snooping();


		for (int i = 0; i < 4; i++) { // all the cores are snooping
			core_i_snoop(i);
		}
		main_mem_snoop();

		bus_logic_after_snooping();

		check_if_req_fulfilled();
	}

}



int main(int argc, char* argv[]) {
	
	//// *** Code for testing the round robin implimentation: ************************************************************************************************************
	//printf("Initialy the arrays are- priorities: {%d, %d, %d, %d} , core_has_request: {%d, %d, %d, %d}\n", priorities[0], priorities[1], priorities[2], priorities[3],
	//	core_has_request[0], core_has_request[1], core_has_request[2], core_has_request[3]);

	//int core_to_serve = choose_core();
	//printf("core_to_serve = %d\n", core_to_serve);
	//printf("Now the arrays are- priorities: {%d, %d, %d, %d} , core_has_request: {%d, %d, %d, %d}\n", priorities[0], priorities[1], priorities[2], priorities[3],
	//	core_has_request[0], core_has_request[1], core_has_request[2], core_has_request[3]);
	//core_has_request[1] = 1;
	//core_has_request[3] = 1;
	//core_to_serve = choose_core();
	//core_used_bus(core_to_serve);
	//printf("Now the arrays are- priorities: {%d, %d, %d, %d} , core_has_request: {%d, %d, %d, %d}\n", priorities[0], priorities[1], priorities[2], priorities[3],
	//	core_has_request[0], core_has_request[1], core_has_request[2], core_has_request[3]);
	//// *******************************************************************************************************************************************************************

	// ******* Code for testing core 0 reading address 0 from the memory: *********************************************************************************************************
	init_caches();
	PR_REQ* pr_req = calloc(1, sizeof(PR_REQ));
	pr_req->type = PRRD;
	pr_req->addr = 0;
	pr_req->core_index = 0;
	main_memory[0] = 2021;
	main_memory[1] = 2022;

	PrRd(pr_req);

	

	// printf("Now the arrays are- core_has_request: {%d, %d, %d, %d}\n", core_has_request[0], core_has_request[1], core_has_request[2], core_has_request[3]);
	
	for (int j = 0; j < 24; j++) {
		if (bus_cmd == NO_CMD) {
			// code for setting a request and activating it
			int core_to_serve = choose_core();
			// printf("core_to_serve = %d\n", core_to_serve);
			if (core_to_serve == -1) continue; // No transaction needed
			curr_request = requests[core_to_serve];
			//printf("core_to_serve = %d, curr_request->core_index = %d\n", core_to_serve, curr_request->core_index);
			core_used_bus(core_to_serve); // update the priority
			requests[core_to_serve] = NULL;
			core_has_request[core_to_serve] = 0;
			place_request_on_bus();
		}

		bus_logic_before_snooping();


		for (int i = 0; i < 4; i++) { // all the cores are snooping
			core_i_snoop(i);
		}
		main_mem_snoop();

		bus_logic_after_snooping();

		check_if_req_fulfilled();

		//printf("bus_cmd = %d\n", bus_cmd);
		printf("done = %d, count_down_to_first_word = %d, bus_addr = %d\n", pr_req->done, count_down_to_first_word, bus_addr);
	}
	printf("data = %d, block 0 in cache- 0: %d, 1: %d, 2: %d, 3: %d\n", pr_req->data, dsrams_array[0]->sram[0][0], dsrams_array[0]->sram[0][1], dsrams_array[0]->sram[0][2], dsrams_array[0]->sram[0][3]);


	free_caches();
	free(pr_req);
	// *********************************************************************************************************************************************************************
	
	
	return 0;
}

