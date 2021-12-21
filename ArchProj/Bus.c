#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Each cache has 256 words (lines of 32 bits), and a block is 4 words so offset is 2 bits.
 * There are 64 blocks in a cache so we need 6 bits for index.
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
#define SHARED               1
#define EXCLUSIVE            2
#define MODIFIED             3



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


DSRAM* dsrams_array[4];

TSRAM* tsrams_array[4];


int priorities[4] = { 0, 1, 2, 3 };
int core_has_request[4] = { 0, 0, 0, 0 };
BUS_REQ* requests[4];
PR_REQ* pr_requests[4];
BUS_REQ* curr_request;

char bus_origid, bus_cmd, flush_offset, modify_after_rdx,
bus_shared, count_down_to_first_word, request_done, words_to_transfer, memory_rd, rd_after_flush, rdx_after_flush, flush_next;

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
			tsram->tags[index] = tag;
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
	
	pr_requests[core_index] = request;

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
			requests[core_index]->data = request->data;
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
			requests[core_index]->data = request->data;
			requests[core_index]->type = FLUSH_BUSRDX; // indicate Flush + BusRdX
			core_has_request[core_index] = 1;
			
		}
		else {
			// Setting a bus request for BusRdX
			requests[core_index] = calloc(1, sizeof(BUS_REQ));
			requests[core_index]->core_index = core_index;
			requests[core_index]->tag = tag;
			requests[core_index]->offset = offset;
			requests[core_index]->addr = addr;
			requests[core_index]->data = request->data;
			requests[core_index]->type = BUSRDX; // indicate BusRdX
			core_has_request[core_index] = 1;
			// Need to check if going to state S or E by snooping!
			
		}

		// ??? we need here to update the tag to the new one ???

		tsram->tags[index] = tag;

		// ???

	}

}


void main_mem_snoop() {

	if (bus_cmd == NO_CMD || bus_origid == 4) { // Checking if we need to snoop
		return;
	}

	if (bus_cmd < 3) { // bus_cmd is 1 or 2
		main_mem_is_wating = 1;
	}
	else { // core flushes
		main_memory[bus_addr] = bus_data;
	}


}


void core_i_snoop(int i) {
	int offset = bus_addr & 0x3;
	int index = (bus_addr >> 2) & 0x3F;
	int tag = (bus_addr >> 8) & 0xFFF;
	TSRAM* tsram = tsrams_array[i];
	DSRAM* dsram = dsrams_array[i];
	if (tsram->tags[index] != tag || bus_cmd == NO_CMD ) { // Checking if we even have the block in the cache and have a command in the bus

		return;
	}
	if (tsram->MESI[index] == INVALID && curr_request->core_index != i) { // ?? not sure about this ??

		return;
	}

	if (bus_origid == i) { // this core is using the bus, no need to do anything because bus logic does it
		return;
	}
	if (bus_cmd == BUSRD) { // BusRd - need to do something if the block is in state M or E
		if (tsram->MESI[index] == MODIFIED) { // state of the block in this core cache is modified so we need to flush the content

			bus_origid = i; // core i
			flush_next = 1; // flush
			bus_addr = bus_addr & 0xFFFFC; // the first word in the block

			bus_data = dsram->sram[index][offset]; // actualy flushing

			tsram->MESI[index] = SHARED; // The block is now in shared state

			return;
		}
		else if (tsram->MESI[index] == EXCLUSIVE) {
			tsram->MESI[index] = SHARED; // The block is now in shared state
		}
		if (tsram->MESI[index] != INVALID) {

			bus_shared = 1; // telling the cache that reads that he is not the only one who have this block
		}
		
	}
	else if (bus_cmd == BUSRDX) { // BusRdX - we invalidate the block anyways and flush if in M mode

		if (tsram->MESI[index] == MODIFIED) { // state of the block in this core cache is modified so we need to flush the content

			bus_origid = i; // core i
			flush_next = 1; // flush
			bus_addr = bus_addr & 0xFFFFC; // the first word in the block
		}
		tsram->MESI[index] = INVALID; // we invalidate this block
		return;
	}
	else if (bus_cmd == FLUSH) { // Flush - some other core is flushing
		dsrams_array[i]->sram[index][offset] = bus_data; // we copy the flushed data because it is the most updated
		//tsram->MESI[index] = SHARED;
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
		if (type == BUSRDX) {
			modify_after_rdx = 1;

		}
	}
	else{
		bus_cmd = FLUSH;
		flush_offset = 0;
		memory_rd = 0; // Memory doesn't need to respond anymore.
		if (type == FLUSH_BUSRD) {
			rd_after_flush = 1;
			bus_addr = ((tsrams_array[bus_origid]->tags[curr_request->index] << 8) | 0x000000FC) & (curr_request->index << 2); // the first word in the block
		}
		else if (type == FLUSH_BUSRDX) {

			rdx_after_flush = 1;
			bus_addr = ((tsrams_array[bus_origid]->tags[curr_request->index] << 8) | 0x000000FC) & (curr_request->index << 2); // the first word in the block
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
		}
		else { // cache is flushing
			bus_data = dsrams_array[bus_origid]->sram[index][flush_offset]; // flushing to bus
		}
	}
}


void bus_logic_after_snooping() {

	if (bus_cmd == FLUSH) {

		if (flush_offset == 3) { // This means this was the last word in the block to flush
			bus_cmd = NO_CMD;
			flush_offset = 0;
			//if (bus_origid != 4) { // if it wasnt the main memory flushing
			//	tsrams_array[bus_origid]->MESI[(bus_addr >> 2) & 0x3F] = SHARED; // now we share the block and everyone can enjoy it
			//}
			if (modify_after_rdx) { // After BsRdx we want to write to the cache
				printf("curr_request->index = %d\n\n", curr_request->index);

				dsrams_array[curr_request->core_index]->sram[(bus_addr >> 2) & 0x3F][curr_request->offset] = curr_request->data;
				tsrams_array[curr_request->core_index]->MESI[(bus_addr >> 2) & 0x3F] = MODIFIED;
				modify_after_rdx = 0;
			}
			if (rd_after_flush) {
				bus_cmd = BUSRD;
				tsrams_array[bus_origid]->tags[curr_request->index] = curr_request->tag; // put the new tag 
				bus_addr = curr_request->addr;
				count_down_to_first_word = 16;
				memory_rd = 1; // Memory needs to respond	
				rd_after_flush = 0;
			}
			else if (rdx_after_flush) {
				bus_cmd = BUSRDX;
				tsrams_array[bus_origid]->tags[curr_request->index] = curr_request->tag; // put the new tag 
				bus_addr = curr_request->addr;
				count_down_to_first_word = 16;
				memory_rd = 1; // Memory needs to respond
				modify_after_rdx = 1;
				rdx_after_flush = 0;
			}
			else {
				curr_request->done = 1; // request fullfiled
			}
			
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
	if (flush_next) {
		bus_cmd = FLUSH;
		flush_next = 0;
	}
}


void check_if_req_fulfilled() {
	if (curr_request->done) {
		PR_REQ* pr_req = pr_requests[curr_request->core_index];
		int core_ind = curr_request->core_index;
		requests[core_ind] = NULL;

		if (pr_req->type == PRRD) { // so core wants to read the updated data
			pr_req->data = dsrams_array[core_ind]->sram[pr_req->index][pr_req->offset]; // giving the core the data from cache
			if (tsrams_array[pr_req->core_index]->MESI[pr_req->index] == INVALID) tsrams_array[pr_req->core_index]->MESI[pr_req->index] = SHARED;
		}
		pr_req->done = 1; // telling the core that it can continue (use the data)
		pr_requests[core_ind] = NULL;
		free(curr_request);
	}
}


void bus_step() {
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


// Main function for testing bus unit
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


	// 
	// ******* Code for testing core 0 reading address 0 from the memory: *********************************************************************************************************
	
	init_caches();
	PR_REQ* pr_req = calloc(1, sizeof(PR_REQ));
	pr_req->type = PRRD;
	pr_req->addr = 0;
	pr_req->core_index = 0;
	//pr_req->tag = 1;
	main_memory[0] = 2021;
	main_memory[1] = 2022;
	main_memory[2] = 2023;
	main_memory[3] = 2024;

	printf("initial parameters:\nmain_memory[0] = %d, main_memory[1] = %d, main_memory[2] = %d, main_memory[3] = %d\n", main_memory[0], main_memory[1], main_memory[2], main_memory[3]);
	printf("block 0 in cache0- 0: %d, 1: %d, 2: %d, 3: %d\n", dsrams_array[0]->sram[0][0], dsrams_array[0]->sram[0][1], dsrams_array[0]->sram[0][2], dsrams_array[0]->sram[0][3]);
	printf("cache0block0 state = %d\n\n", tsrams_array[0]->MESI[0]);
	printf("Core 0 reading address 0\n\n");


	PrRd(pr_req);

	// printf("Now the arrays are- core_has_request: {%d, %d, %d, %d}\n", core_has_request[0], core_has_request[1], core_has_request[2], core_has_request[3]);
	
	for (int j = 0; j < 20; j++) {
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

		//printf("j = %d, cache0block0 state = %d\n",j ,tsrams_array[0]->MESI[0]);
		//printf("bus_shared = %d\n", bus_shared);
		//printf("done = %d, count_down_to_first_word = %d, bus_addr = %d\n", pr_req->done, count_down_to_first_word, bus_addr);
	}
	
	//printf("core_has_request[0]= %d\n", core_has_request[0]);
	printf("data = %d, block 0 in cache0- 0: %d, 1: %d, 2: %d, 3: %d\n", pr_req->data, dsrams_array[0]->sram[0][0], dsrams_array[0]->sram[0][1], dsrams_array[0]->sram[0][2], dsrams_array[0]->sram[0][3]);
	printf("cache0block0 state = %d\n", tsrams_array[0]->MESI[0]);
	printf("cache1block1 state = %d\n\n", tsrams_array[1]->MESI[0]);
	//printf("cache1block1 state = %d\n", tsrams_array[2]->MESI[0]);
	//printf("cache1block1 state = %d\n", tsrams_array[3]->MESI[0]);
	free(pr_req);
	
	// now cache 0 has in block 0 the first block of main memory (E state)
	// lets try to read it (cache hit), should just bring the data without the bus
	// ******* Code for testing core 0 reading address 1: *********************************************************************************************************

	printf("now cache 0 has in block 0 the first block of main memory (E state)\n");
	printf("lets try to read it (cache hit), should just bring the data without the bus\n");
	printf("\ncore 0 reading address 1\n\n");

	pr_req = calloc(1, sizeof(PR_REQ));
	pr_req->type = PRRD;
	pr_req->addr = 1;
	pr_req->core_index = 0;
	pr_req->offset = 1;

	PrRd(pr_req);

	printf("data = %d, block 0 in cache 0- 0: %d, 1: %d, 2: %d, 3: %d\n", pr_req->data, dsrams_array[0]->sram[0][0], dsrams_array[0]->sram[0][1], dsrams_array[0]->sram[0][2], dsrams_array[0]->sram[0][3]);
	//data = 2023, block 0 in cache 0- 0: 2021, 1: 2023, 2: 0, 3: 0
	// printf("done = %d\n", pr_req->done);
	printf("cache0block0 state = %d\n", tsrams_array[0]->MESI[0]);
	printf("cache1block1 state = %d\n\n", tsrams_array[1]->MESI[0]);
	free(pr_req);


	// Still cache 0 has in block 0 the first block of main memory (E state)
	// lets try to write to it with core 0 (should just change the value in the cache and the state to M)
	// ******* Code for testing core 0 writing to address 1: *********************************************************************************************************
	
	printf("Still cache 0 has in block 0 the first block of main memory (E state)\n");
	printf("lets try to write to it with core 0 (should just change the value in the cache and the state to M)\n");
	printf("\ncore 0 writing 2023 to address 1\n\n");

	pr_req = calloc(1, sizeof(PR_REQ));
	pr_req->type = PRWR;
	pr_req->addr = 1;
	pr_req->core_index = 0;
	pr_req->data = 2023;
	pr_req->offset = 1;

	PrWr(pr_req); // no need here for using the bus

	printf("data = %d, done = %d, block 0 in cache 0- 0: %d, 1: %d, 2: %d, 3: %d\n", pr_req->data, pr_req->done, dsrams_array[0]->sram[0][0], dsrams_array[0]->sram[0][1], dsrams_array[0]->sram[0][2], dsrams_array[0]->sram[0][3]);
	//data = 2023, block 0 in cache 0- 0: 2021, 1: 2023, 2: 0, 3: 0
	// printf("done = %d\n", pr_req->done);
	printf("cache0block0 state = %d\n", tsrams_array[0]->MESI[0]);
	printf("cache1block1 state = %d\n\n", tsrams_array[1]->MESI[0]);
	free(pr_req);

	// now cache 0 has in block 0 the first block of main memory (M state)
	// lets try to read it with core 0 (should not involve the bus)
	// ******* Code for testing core 1 reading address 1: *********************************************************************************************************

	printf("now cache 0 has in block 0 the first block of main memory (M state)\n");
	printf("lets try to read it with core 0 (should not involve the bus)\n");
	printf("\ncore 0 reading address 1\n\n");
	
	pr_req = calloc(1, sizeof(PR_REQ));
	pr_req->type = PRRD;
	pr_req->addr = 1;
	pr_req->core_index = 0;
	pr_req->offset = 1;

	PrRd(pr_req);

	printf("data = %d, block 0 in cache 0- 0: %d, 1: %d, 2: %d, 3: %d\n", pr_req->data, dsrams_array[0]->sram[0][0], dsrams_array[0]->sram[0][1], dsrams_array[0]->sram[0][2], dsrams_array[0]->sram[0][3]);
	//data = 2023, block 0 in cache 0- 0: 2021, 1: 2023, 2: 0, 3: 0
	// printf("done = %d\n", pr_req->done);
	printf("cache0block0 state = %d\n", tsrams_array[0]->MESI[0]);
	printf("cache1block1 state = %d\n\n", tsrams_array[1]->MESI[0]);
	free(pr_req);

	// Still cache 0 has in block 0 the first block of main memory (M state)
	// lets try to write ti it with core 0 (should not involve the bus)
	// ******* Code for testing core 1 reading address 1: *********************************************************************************************************

	printf("Still cache 0 has in block 0 the first block of main memory (M state)\n");
	printf("lets try to read it with core 1 (cache 0 should flush its content and move to state S)\n");
	printf("\ncore 1 reading address 1\n\n");

	pr_req = calloc(1, sizeof(PR_REQ));
	pr_req->type = PRWR;
	pr_req->addr = 1;
	pr_req->core_index = 0;
	pr_req->data = 1984;
	pr_req->offset = 1;

	PrWr(pr_req); // no need here for using the bus

	printf("data = %d, done = %d, block 0 in cache 0- 0: %d, 1: %d, 2: %d, 3: %d\n", pr_req->data, pr_req->done, dsrams_array[0]->sram[0][0], dsrams_array[0]->sram[0][1], dsrams_array[0]->sram[0][2], dsrams_array[0]->sram[0][3]);
	//data = 2023, block 0 in cache 0- 0: 2021, 1: 2023, 2: 0, 3: 0
	// printf("done = %d\n", pr_req->done);
	printf("cache0block0 state = %d\n", tsrams_array[0]->MESI[0]);
	printf("cache1block1 state = %d\n\n", tsrams_array[1]->MESI[0]);
	free(pr_req);

	// Still cache 0 has in block 0 the first block of main memory (M state)
	// lets try to read it with core 1 (cache 0 should flush its content and move to state S)
	// ******* Code for testing core 1 reading address 1: *********************************************************************************************************

	printf("Still cache 0 has in block 0 the first block of main memory (M state)\n");
	printf("lets try to read it with core 1 (cache 0 should flush its content and move to state S)\n");
	printf("\ncore 1 reading address 1\n\n");

	pr_req = calloc(1, sizeof(PR_REQ));
	pr_req->type = PRRD;
	pr_req->addr = 1;
	pr_req->core_index = 1;
	pr_req->offset = 1;

	PrRd(pr_req);

	//printf("core_has_request[0]= %d\n", core_has_request[0]);

	for (int j = 0; j < 20; j++) {
		if (bus_cmd == NO_CMD) {
				// code for setting a request and activating it
			//printf("j = %d\n", j);
				int core_to_serve = choose_core();
				//printf("core_to_serve = %d\n", core_to_serve);
				// printf("core_to_serve = %d\n", core_to_serve);
				if (core_to_serve == -1) continue; // no transaction needed
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

			//printf("bus_shared = %d\n", bus_shared);
			//printf("iteration = %d, bus_addr = %d, bus_data = %d, bus_origid = %d, bus_cmd = %d\n", j, bus_addr, bus_data, bus_origid, bus_cmd);
			//printf("done = %d, count_down_to_first_word = %d, bus_addr = %d\n", pr_req->done, count_down_to_first_word, bus_addr);
		}
	
	printf("data = %d, block 0 in cache 1- 0: %d, 1: %d, 2: %d, 3: %d\n", pr_req->data, dsrams_array[1]->sram[0][0], dsrams_array[1]->sram[0][1], dsrams_array[1]->sram[0][2], dsrams_array[1]->sram[0][3]);
	printf("cache0block0 state = %d\n", tsrams_array[0]->MESI[0]);
	printf("cache1block0 state = %d\n", tsrams_array[1]->MESI[0]);
	//printf("cache1block0 state = %d\n", tsrams_array[2]->MESI[0]);
	//printf("cache1block0 state = %d\n", tsrams_array[3]->MESI[0]);
	printf("main_memory[0] = %d, main_memory[1] = %d, main_memory[2] = %d, main_memory[3] = %d\n\n", main_memory[0], main_memory[1], main_memory[2], main_memory[3]);


	free(pr_req);
	
	// now cache 0 and 1 has in block 0 the first block of main memory (S state)
	// lets try to read with core 0 address 1 (should not involve the bus and stay in S)
	// ******* Code for testing core 0 reading address 1: *********************************************************************************************************

	printf("now cache 0 and 1 has in block 0 the first block of main memory (S state)\n");
	printf("lets try to read with core 0 address 1 (should not involve the bus)\n");
	printf("\ncore 0 reading address 1\n\n");
	
	pr_req = calloc(1, sizeof(PR_REQ));
	pr_req->type = PRRD;
	pr_req->addr = 1;
	pr_req->core_index = 0;
	pr_req->offset = 1;

	PrRd(pr_req);

	printf("data = %d, block 0 in cache 0- 0: %d, 1: %d, 2: %d, 3: %d\n", pr_req->data, dsrams_array[0]->sram[0][0], dsrams_array[0]->sram[0][1], dsrams_array[0]->sram[0][2], dsrams_array[0]->sram[0][3]);
	//data = 2023, block 0 in cache 0- 0: 2021, 1: 2023, 2: 0, 3: 0
	// printf("done = %d\n", pr_req->done);
	printf("cache0block0 state = %d\n", tsrams_array[0]->MESI[0]);
	printf("cache1block0 state = %d\n\n", tsrams_array[1]->MESI[0]);
	free(pr_req);


	// Still cache 0 and 1 has in block 0 the first block of main memory (S state)
	// lets try to write with core 1 to address 0 (should do rdx and move to state M and core 0 to state I)
	// ******* Code for testing core 1 writing address 0: *********************************************************************************************************
	
	printf("Still cache 0 and 1 has in block 0 the first block of main memory (S state)\n");
	printf("lets try to write with core 1 to address 0 (should do rdx and move to state M and core 0 to state I)\n");
	printf("\ncore 1 writing 2020 to address 0\n\n");

	pr_req = calloc(1, sizeof(PR_REQ));
	pr_req->type = PRWR;
	pr_req->addr = 0;
	pr_req->core_index = 1;
	pr_req->offset = 0;
	pr_req->data = 2020;

	PrWr(pr_req);

	for (int j = 0; j < 20; j++) {
		if (bus_cmd == NO_CMD) {
			// code for setting a request and activating it
			//printf("j = %d\n", j);
			int core_to_serve = choose_core();
			//printf("core_to_serve = %d\n", core_to_serve);
			// printf("core_to_serve = %d\n", core_to_serve);
			if (core_to_serve == -1) continue; // no transaction needed
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
	
		//printf("bus_shared = %d\n", bus_shared);

		//printf("curr_request->core_index = %d\n", curr_request->core_index);
		//printf("iteration = %d, bus_addr = %d, bus_data = %d, bus_origid = %d, bus_cmd = %d\n", j, bus_addr, bus_data, bus_origid, bus_cmd);
		//printf("done = %d, count_down_to_first_word = %d, bus_addr = %d\n", pr_req->done, count_down_to_first_word, bus_addr);
	}

	printf("data = %d, block 0 in cache 1- 0: %d, 1: %d, 2: %d, 3: %d\n", pr_req->data, dsrams_array[1]->sram[0][0], dsrams_array[1]->sram[0][1], dsrams_array[1]->sram[0][2], dsrams_array[1]->sram[0][3]);
	printf("cache0block0 state = %d\n", tsrams_array[0]->MESI[0]);
	printf("cache1block0 state = %d\n", tsrams_array[1]->MESI[0]);
	printf("main_memory[0] = %d, main_memory[1] = %d, main_memory[2] = %d, main_memory[3] = %d\n\n", main_memory[0], main_memory[1], main_memory[2], main_memory[3]);

	free(pr_req);

	// now cache 0 block 0 is invalid and cache 1 block 1 is modified
	// lets try to write with core 1 to address 64 X 4, this address has index 0 but tag 1 (should flush the content and then do rdx and stay in state M)
	// ******* Code for testing core 1 writing address 256: *********************************************************************************************************

	printf("now cache 0 block 0 is invalid and cache 1 block 1 is modified\n");
	printf("lets try to write with core 1 to address 64 X 4 = 256 (b100000000), \nthis address has index 0 but tag 1 (should flush the content and then do rdx and stay in state M)\n");
	printf("\ncore 1 writing 1996 to address 256\n\n");

	pr_req = calloc(1, sizeof(PR_REQ));
	pr_req->type = PRWR;
	pr_req->addr = 256;
	pr_req->core_index = 1;
	pr_req->tag = 1;
	pr_req->offset = 0;
	pr_req->data = 1996;

	PrWr(pr_req);

	for (int j = 0; j < 30; j++) {
		if (bus_cmd == NO_CMD) {
			// code for setting a request and activating it
			//printf("j = %d\n", j);
			int core_to_serve = choose_core();
			//printf("core_to_serve = %d\n", core_to_serve);
			// printf("core_to_serve = %d\n", core_to_serve);
			if (core_to_serve == -1) continue; // no transaction needed
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

		//printf("bus_shared = %d\n", bus_shared);

		//printf("curr_request->core_index = %d\n", curr_request->core_index);
		//printf("iteration = %d, bus_addr = %d, bus_data = %d, bus_origid = %d, bus_cmd = %d\n", j, bus_addr, bus_data, bus_origid, bus_cmd);
		//printf("done = %d, count_down_to_first_word = %d, bus_addr = %d\n", pr_req->done, count_down_to_first_word, bus_addr);
	}

	printf("data = %d, block 0 in cache 0- 0: %d, 1: %d, 2: %d, 3: %d\n", pr_req->data, dsrams_array[0]->sram[0][0], dsrams_array[0]->sram[0][1], dsrams_array[0]->sram[0][2], dsrams_array[0]->sram[0][3]);
	printf("data = %d, block 0 in cache 1- 0: %d, 1: %d, 2: %d, 3: %d\n", pr_req->data, dsrams_array[1]->sram[0][0], dsrams_array[1]->sram[0][1], dsrams_array[1]->sram[0][2], dsrams_array[1]->sram[0][3]);
	printf("cache0block0 state = %d\n", tsrams_array[0]->MESI[0]);
	printf("cache1block0 state = %d\n", tsrams_array[1]->MESI[0]);
	printf("main_memory[0] = %d, main_memory[1] = %d, main_memory[2] = %d, main_memory[3] = %d\n", main_memory[0], main_memory[1], main_memory[2], main_memory[3]);
	printf("main_memory[256] = %d, main_memory[257] = %d, main_memory[258] = %d, main_memory[259] = %d\n\n", main_memory[256], main_memory[257], main_memory[258], main_memory[259]);
	free(pr_req);

	// lets try to write 1900 with core 0 to address 257, this should move it to state M and invalidate cache 1 
	// ****************************************************************************************************************

	printf("lets try to write 1900 with core 0 to address 257, this should move it to state M and invalidate cache 1\n\n");

	pr_req = calloc(1, sizeof(PR_REQ));
	pr_req->type = PRWR;
	pr_req->addr = 257;
	pr_req->core_index = 0;
	pr_req->tag = 1;
	pr_req->offset = 1;
	pr_req->data = 1900;

	PrWr(pr_req);

	for (int j = 0; j < 30; j++) {
		if (bus_cmd == NO_CMD) {
			// code for setting a request and activating it
			//printf("j = %d\n", j);
			int core_to_serve = choose_core();
			//printf("core_to_serve = %d\n", core_to_serve);
			// printf("core_to_serve = %d\n", core_to_serve);
			if (core_to_serve == -1) continue; // no transaction needed
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

		//printf("cache1block0 state = %d\n", tsrams_array[1]->MESI[0]);

		//printf("bus_shared = %d\n", bus_shared);

		//printf("curr_request->core_index = %d\n", curr_request->core_index);
		//printf("iteration = %d, bus_addr = %d, bus_data = %d, bus_origid = %d, bus_cmd = %d\n", j, bus_addr, bus_data, bus_origid, bus_cmd);
		//printf("done = %d, count_down_to_first_word = %d, bus_addr = %d\n", pr_req->done, count_down_to_first_word, bus_addr);
	}

	printf("data = %d, block 0 in cache 0- 0: %d, 1: %d, 2: %d, 3: %d\n", pr_req->data, dsrams_array[0]->sram[0][0], dsrams_array[0]->sram[0][1], dsrams_array[0]->sram[0][2], dsrams_array[0]->sram[0][3]);
	printf("data = %d, block 0 in cache 1- 0: %d, 1: %d, 2: %d, 3: %d\n", pr_req->data, dsrams_array[1]->sram[0][0], dsrams_array[1]->sram[0][1], dsrams_array[1]->sram[0][2], dsrams_array[1]->sram[0][3]);
	printf("cache0block0 state = %d\n", tsrams_array[0]->MESI[0]);
	printf("cache1block0 state = %d\n", tsrams_array[1]->MESI[0]);
	printf("main_memory[0] = %d, main_memory[1] = %d, main_memory[2] = %d, main_memory[3] = %d\n", main_memory[0], main_memory[1], main_memory[2], main_memory[3]);
	printf("main_memory[256] = %d, main_memory[257] = %d, main_memory[258] = %d, main_memory[259] = %d\n\n", main_memory[256], main_memory[257], main_memory[258], main_memory[259]);
	free(pr_req);

	// lets try to read address 0 with core 1, should move to state E  
	// ****************************************************************************************************************

	printf("lets try to read address 0 with core 1, should move to state E\n\n");

	pr_req = calloc(1, sizeof(PR_REQ));
	pr_req->type = PRRD;
	pr_req->addr = 0;
	pr_req->core_index = 1;

	PrRd(pr_req);

	// printf("Now the arrays are- core_has_request: {%d, %d, %d, %d}\n", core_has_request[0], core_has_request[1], core_has_request[2], core_has_request[3]);

	for (int j = 0; j < 20; j++) {
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

		//printf("iteration = %d, bus_addr = %d, bus_data = %d, bus_origid = %d, bus_cmd = %d\n", j, bus_addr, bus_data, bus_origid, bus_cmd);
		//printf("j = %d, cache0block0 state = %d\n",j ,tsrams_array[0]->MESI[0]);
		//printf("bus_shared = %d\n", bus_shared);
		//printf("done = %d, count_down_to_first_word = %d, bus_addr = %d\n", pr_req->done, count_down_to_first_word, bus_addr);
	}

	//printf("core_has_request[0]= %d\n", core_has_request[0]);
	printf("data = %d, block 0 in cache0- 0: %d, 1: %d, 2: %d, 3: %d\n", pr_req->data, dsrams_array[0]->sram[0][0], dsrams_array[0]->sram[0][1], dsrams_array[0]->sram[0][2], dsrams_array[0]->sram[0][3]);
	printf("data = %d, block 0 in cache 1- 0: %d, 1: %d, 2: %d, 3: %d\n", pr_req->data, dsrams_array[1]->sram[0][0], dsrams_array[1]->sram[0][1], dsrams_array[1]->sram[0][2], dsrams_array[1]->sram[0][3]);
	printf("cache0block0 state = %d\n", tsrams_array[0]->MESI[0]);
	printf("cache1block0 state = %d\n\n", tsrams_array[1]->MESI[0]);
	//printf("cache1block1 state = %d\n", tsrams_array[2]->MESI[0]);
	//printf("cache1block1 state = %d\n", tsrams_array[3]->MESI[0]);
	free(pr_req);

	// lets try to read address 0 with core 2, cache 1 and 2 should move to state S  
	// ****************************************************************************************************************

	printf("lets try to read address 0 with core 2, cache 1 and 2 should move to state S\n\n");

	pr_req = calloc(1, sizeof(PR_REQ));
	pr_req->type = PRRD;
	pr_req->addr = 0;
	pr_req->core_index = 2;

	PrRd(pr_req);

	// printf("Now the arrays are- core_has_request: {%d, %d, %d, %d}\n", core_has_request[0], core_has_request[1], core_has_request[2], core_has_request[3]);

	for (int j = 0; j < 20; j++) {
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

		//printf("iteration = %d, bus_addr = %d, bus_data = %d, bus_origid = %d, bus_cmd = %d\n", j, bus_addr, bus_data, bus_origid, bus_cmd);
		//printf("j = %d, cache0block0 state = %d\n",j ,tsrams_array[0]->MESI[0]);
		//printf("bus_shared = %d\n", bus_shared);
		//printf("done = %d, count_down_to_first_word = %d, bus_addr = %d\n", pr_req->done, count_down_to_first_word, bus_addr);
	}

	//printf("core_has_request[0]= %d\n", core_has_request[0]);
	printf("data = %d, block 0 in cache0- 0: %d, 1: %d, 2: %d, 3: %d\n", pr_req->data, dsrams_array[0]->sram[0][0], dsrams_array[0]->sram[0][1], dsrams_array[0]->sram[0][2], dsrams_array[0]->sram[0][3]);
	printf("data = %d, block 0 in cache 1- 0: %d, 1: %d, 2: %d, 3: %d\n", pr_req->data, dsrams_array[1]->sram[0][0], dsrams_array[1]->sram[0][1], dsrams_array[1]->sram[0][2], dsrams_array[1]->sram[0][3]);
	printf("data = %d, block 0 in cache 2- 0: %d, 1: %d, 2: %d, 3: %d\n", pr_req->data, dsrams_array[2]->sram[0][0], dsrams_array[2]->sram[0][1], dsrams_array[2]->sram[0][2], dsrams_array[2]->sram[0][3]);
	printf("cache0block0 state = %d\n", tsrams_array[0]->MESI[0]);
	printf("cache1block0 state = %d\n", tsrams_array[1]->MESI[0]);
	printf("cache2block0 state = %d\n\n", tsrams_array[2]->MESI[0]);
	//printf("cache1block1 state = %d\n", tsrams_array[3]->MESI[0]);
	free(pr_req);


	main_memory[4] = 1234;
	printf("main_memory[4] = %d\n\n", main_memory[4]);
	// lets try to read address 4 with core 3, should move to state E (index is 1) 
	// ****************************************************************************************************************

	printf("lets try to read address 4 with core 3, should move to state E (index is 1)\n\n");

	pr_req = calloc(1, sizeof(PR_REQ));
	pr_req->type = PRRD;
	pr_req->addr = 4;
	pr_req->core_index = 3;
	pr_req->index = 1;

	PrRd(pr_req);

	// printf("Now the arrays are- core_has_request: {%d, %d, %d, %d}\n", core_has_request[0], core_has_request[1], core_has_request[2], core_has_request[3]);

	for (int j = 0; j < 20; j++) {
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

		//printf("iteration = %d, bus_addr = %d, bus_data = %d, bus_origid = %d, bus_cmd = %d\n", j, bus_addr, bus_data, bus_origid, bus_cmd);
		//printf("j = %d, cache0block0 state = %d\n",j ,tsrams_array[0]->MESI[0]);
		//printf("bus_shared = %d\n", bus_shared);
		//printf("done = %d, count_down_to_first_word = %d, bus_addr = %d\n", pr_req->done, count_down_to_first_word, bus_addr);
	}

	//printf("core_has_request[0]= %d\n", core_has_request[0]);
	printf("data = %d, block 0 in cache0- 0: %d, 1: %d, 2: %d, 3: %d\n", pr_req->data, dsrams_array[0]->sram[0][0], dsrams_array[0]->sram[0][1], dsrams_array[0]->sram[0][2], dsrams_array[0]->sram[0][3]);
	printf("data = %d, block 0 in cache 1- 0: %d, 1: %d, 2: %d, 3: %d\n", pr_req->data, dsrams_array[1]->sram[0][0], dsrams_array[1]->sram[0][1], dsrams_array[1]->sram[0][2], dsrams_array[1]->sram[0][3]);
	printf("data = %d, block 1 in cache 1- 0: %d, 1: %d, 2: %d, 3: %d\n", pr_req->data, dsrams_array[1]->sram[1][0], dsrams_array[1]->sram[1][1], dsrams_array[1]->sram[1][2], dsrams_array[1]->sram[1][3]);
	printf("data = %d, block 0 in cache 2- 0: %d, 1: %d, 2: %d, 3: %d\n", pr_req->data, dsrams_array[2]->sram[0][0], dsrams_array[2]->sram[0][1], dsrams_array[2]->sram[0][2], dsrams_array[2]->sram[0][3]);
	printf("data = %d, block 1 in cache 2- 0: %d, 1: %d, 2: %d, 3: %d\n", pr_req->data, dsrams_array[3]->sram[1][0], dsrams_array[3]->sram[1][1], dsrams_array[3]->sram[1][2], dsrams_array[3]->sram[1][3]);
	printf("cache0block0 state = %d\n", tsrams_array[0]->MESI[0]);
	printf("cache1block0 state = %d\n", tsrams_array[1]->MESI[0]);
	printf("cache1block1 state = %d\n", tsrams_array[1]->MESI[1]);
	printf("cache2block0 state = %d\n", tsrams_array[2]->MESI[0]);
	printf("cache3block1 state = %d\n\n", tsrams_array[3]->MESI[1]);
	free(pr_req);

	// now we want to write with core 1 to address 5 (index is 1), should move it to state M and cache3block1 to state I
	// ****************************************************************************************************************

	printf("now we want to write with core 1 to address 5 (index is 1), should move it to state M and cache3block1 to state I\n\n");

	pr_req = calloc(1, sizeof(PR_REQ));
	pr_req->type = PRWR;
	pr_req->addr = 5;
	pr_req->core_index = 1;
	pr_req->index = 1;
	pr_req->offset = 1;
	pr_req->data = 4321;

	PrWr(pr_req);

	for (int j = 0; j < 30; j++) {
		if (bus_cmd == NO_CMD) {
			// code for setting a request and activating it
			//printf("j = %d\n", j);
			int core_to_serve = choose_core();
			//printf("core_to_serve = %d\n", core_to_serve);
			// printf("core_to_serve = %d\n", core_to_serve);
			if (core_to_serve == -1) continue; // no transaction needed
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

		//printf("cache1block0 state = %d\n", tsrams_array[1]->MESI[0]);

		//printf("bus_shared = %d\n", bus_shared);

		//printf("curr_request->core_index = %d\n", curr_request->core_index);
		//printf("iteration = %d, bus_addr = %d, bus_data = %d, bus_origid = %d, bus_cmd = %d\n", j, bus_addr, bus_data, bus_origid, bus_cmd);
		//printf("done = %d, count_down_to_first_word = %d, bus_addr = %d\n", pr_req->done, count_down_to_first_word, bus_addr);
	}

	printf("data = %d, block 0 in cache0- 0: %d, 1: %d, 2: %d, 3: %d\n", pr_req->data, dsrams_array[0]->sram[0][0], dsrams_array[0]->sram[0][1], dsrams_array[0]->sram[0][2], dsrams_array[0]->sram[0][3]);
	printf("data = %d, block 0 in cache 1- 0: %d, 1: %d, 2: %d, 3: %d\n", pr_req->data, dsrams_array[1]->sram[0][0], dsrams_array[1]->sram[0][1], dsrams_array[1]->sram[0][2], dsrams_array[1]->sram[0][3]);
	printf("data = %d, block 1 in cache 1- 0: %d, 1: %d, 2: %d, 3: %d\n", pr_req->data, dsrams_array[1]->sram[1][0], dsrams_array[1]->sram[1][1], dsrams_array[1]->sram[1][2], dsrams_array[1]->sram[1][3]);
	printf("data = %d, block 0 in cache 2- 0: %d, 1: %d, 2: %d, 3: %d\n", pr_req->data, dsrams_array[2]->sram[0][0], dsrams_array[2]->sram[0][1], dsrams_array[2]->sram[0][2], dsrams_array[2]->sram[0][3]);
	printf("data = %d, block 1 in cache 2- 0: %d, 1: %d, 2: %d, 3: %d\n", pr_req->data, dsrams_array[3]->sram[1][0], dsrams_array[3]->sram[1][1], dsrams_array[3]->sram[1][2], dsrams_array[3]->sram[1][3]);
	printf("cache0block0 state = %d\n", tsrams_array[0]->MESI[0]);
	printf("cache1block0 state = %d\n", tsrams_array[1]->MESI[0]);
	printf("cache1block1 state = %d\n", tsrams_array[1]->MESI[1]);
	printf("cache2block0 state = %d\n", tsrams_array[2]->MESI[0]);
	printf("cache3block1 state = %d\n\n", tsrams_array[3]->MESI[1]);
	printf("cache0block0 tag = %d\n", tsrams_array[0]->tags[0]);
	printf("cache1block0 tag = %d\n", tsrams_array[1]->tags[0]);
	printf("cache1block1 tag = %d\n", tsrams_array[1]->tags[1]);
	printf("cache2block0 tag = %d\n", tsrams_array[2]->tags[0]);
	printf("cache3block1 tag = %d\n\n", tsrams_array[3]->tags[1]);
	printf("main_memory[0] = %d, main_memory[1] = %d, main_memory[2] = %d, main_memory[3] = %d\n", main_memory[0], main_memory[1], main_memory[2], main_memory[3]);
	printf("main_memory[4] = %d, main_memory[5] = %d, main_memory[6] = %d, main_memory[7] = %d\n", main_memory[4], main_memory[5], main_memory[6], main_memory[7]);
	printf("main_memory[256] = %d, main_memory[257] = %d, main_memory[258] = %d, main_memory[259] = %d\n\n", main_memory[256], main_memory[257], main_memory[258], main_memory[259]);
	free(pr_req);

	// now we want to write with core 0 to address 0 (index is 0, tag is 0), should make cache 0 flush and then do busRdx and stay at state M. cache1block0 and cache2block0 should move to state I
	// ****************************************************************************************************************

	printf("now we want to write with core 0 to address 0 (index is 0, tag is 0), should make cache 0 flush and then do busRdx and stay at state M. \ncache1block0 and cache2block0 should move to state I\n\n");

	pr_req = calloc(1, sizeof(PR_REQ));
	pr_req->type = PRWR;
	pr_req->addr = 0;
	pr_req->core_index = 0;
	pr_req->index = 0;
	pr_req->offset = 0;
	pr_req->data = 2001;

	PrWr(pr_req);

	for (int j = 0; j < 30; j++) {
		if (bus_cmd == NO_CMD) {
			// code for setting a request and activating it
			//printf("j = %d\n", j);
			int core_to_serve = choose_core();
			//printf("core_to_serve = %d\n", core_to_serve);
			// printf("core_to_serve = %d\n", core_to_serve);
			if (core_to_serve == -1) continue; // no transaction needed
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

		//printf("cache1block0 state = %d\n", tsrams_array[1]->MESI[0]);

		//printf("bus_shared = %d\n", bus_shared);

		//printf("curr_request->core_index = %d\n", curr_request->core_index);
		//printf("iteration = %d, bus_addr = %d, bus_data = %d, bus_origid = %d, bus_cmd = %d\n", j, bus_addr, bus_data, bus_origid, bus_cmd);
		//printf("done = %d, count_down_to_first_word = %d, bus_addr = %d\n", pr_req->done, count_down_to_first_word, bus_addr);
	}

	printf("data = %d, block 0 in cache0- 0: %d, 1: %d, 2: %d, 3: %d\n", pr_req->data, dsrams_array[0]->sram[0][0], dsrams_array[0]->sram[0][1], dsrams_array[0]->sram[0][2], dsrams_array[0]->sram[0][3]);
	printf("data = %d, block 0 in cache 1- 0: %d, 1: %d, 2: %d, 3: %d\n", pr_req->data, dsrams_array[1]->sram[0][0], dsrams_array[1]->sram[0][1], dsrams_array[1]->sram[0][2], dsrams_array[1]->sram[0][3]);
	printf("data = %d, block 1 in cache 1- 0: %d, 1: %d, 2: %d, 3: %d\n", pr_req->data, dsrams_array[1]->sram[1][0], dsrams_array[1]->sram[1][1], dsrams_array[1]->sram[1][2], dsrams_array[1]->sram[1][3]);
	printf("data = %d, block 0 in cache 2- 0: %d, 1: %d, 2: %d, 3: %d\n", pr_req->data, dsrams_array[2]->sram[0][0], dsrams_array[2]->sram[0][1], dsrams_array[2]->sram[0][2], dsrams_array[2]->sram[0][3]);
	printf("data = %d, block 1 in cache 2- 0: %d, 1: %d, 2: %d, 3: %d\n", pr_req->data, dsrams_array[3]->sram[1][0], dsrams_array[3]->sram[1][1], dsrams_array[3]->sram[1][2], dsrams_array[3]->sram[1][3]);
	printf("cache0block0 state = %d\n", tsrams_array[0]->MESI[0]);
	printf("cache1block0 state = %d\n", tsrams_array[1]->MESI[0]);
	printf("cache1block1 state = %d\n", tsrams_array[1]->MESI[1]);
	printf("cache2block0 state = %d\n", tsrams_array[2]->MESI[0]);
	printf("cache3block1 state = %d\n\n", tsrams_array[3]->MESI[1]);
	printf("main_memory[0] = %d, main_memory[1] = %d, main_memory[2] = %d, main_memory[3] = %d\n", main_memory[0], main_memory[1], main_memory[2], main_memory[3]);
	printf("main_memory[4] = %d, main_memory[5] = %d, main_memory[6] = %d, main_memory[7] = %d\n", main_memory[4], main_memory[5], main_memory[6], main_memory[7]);
	printf("main_memory[256] = %d, main_memory[257] = %d, main_memory[258] = %d, main_memory[259] = %d\n\n", main_memory[256], main_memory[257], main_memory[258], main_memory[259]);



	

	free_caches();
	free(pr_req);
	// *********************************************************************************************************************************************************************
	
	
	return 0;
}

