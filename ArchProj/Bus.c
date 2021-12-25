#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bus.h"

/* Each cache has 256 words (lines of 32 bits), and a block is 4 words so offset is 2 bits.
 * There are 64 blocks in a cache so we need 6 bits for index.
 * The rest 12 bits of the adress is tag (the address is 20 bits).
 * We have 2^20 words in main memory so 2^18 blocks
 */




// int main_memory[BLOCKS_IN_MAIN_MEM][WORDS_IN_BLOCK];
int main_mem_timer = 0;
int main_mem_is_wating = 0;



DSRAM* dsrams_array[4];

TSRAM* tsrams_array[4];


int priorities[4] = { 0, 1, 2, 3 };
int core_has_request[4] = { 0, 0, 0, 0 };

PR_REQ* pr_requests[4];
BUS_REQ* curr_request;

char bus_origid, bus_cmd, flush_offset, modify_after_rdx,
bus_shared, request_done, words_to_transfer, memory_rd, rd_after_flush, rdx_after_flush, flush_next;

int count_down_to_first_word;
int bus_addr, bus_data;

cachestall[4] = { 0 };


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



// This is a function that a core can call in order to request reading data from the cache. 
// when the request is served the field done will be 1 and the requested word will be in field data
void PrRd(PR_REQ* request) {
	int core_index = request->core_index;
	int addr = request->addr;
	int offset = addr & 0x3;
	int index = (addr >> 2) & 0x3F;
	int tag = (addr >> 8) & 0xFFF;


	TSRAM* tsram = tsrams_array[core_index];
	DSRAM* dsram = dsrams_array[core_index];

	pr_requests[core_index] = request;

	if (tag == tsram->tags[index]) { // If the block is in the cache 
		if (tsram->MESI[index]) { // The state is all but invalid (not 0, cache hit) 
			request->data = dsram->sram[index][offset]; // we take the content and do not involve the bus
			request->done = 1;
			/*printf("PO\n");*/
			return;
		}
		else { // The block is invalid (state I)
			// Setting a bus request for reading
			//printf("LO PO\n");
			requests[core_index] = calloc(1, sizeof(BUS_REQ));
			requests[core_index]->core_index = core_index;
			requests[core_index]->tag = tag;
			requests[core_index]->offset = offset;
			requests[core_index]->addr = addr;
			requests[core_index]->index = index;
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
			requests[core_index]->index = index;
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
			requests[core_index]->index = index;
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

	if (request == NULL) {
		return;
	}
	
	int core_index = request->core_index;
	int addr = request->addr;
	int index = request->index;
	int tag = request->tag;
	int offset = request->offset;

	TSRAM* tsram = tsrams_array[core_index];
	DSRAM* dsram = dsrams_array[core_index];
	
	pr_requests[core_index] = request;
	printf("%d =?= %d | [%d]%d\n", tag, tsram->tags[index], index, tsram->MESI[index]);
	if (tag == tsram->tags[index]) { // If the block is in the cache
		if (tsram->MESI[index] == MODIFIED) { // the state is M so we just modify the content
			dsram->sram[index][offset] = request->data;
			request->done = 1;
			printf("PO\n");
			return;
		}
		else if (tsram->MESI[index] == EXCLUSIVE) { // state is E so we modify the content and go to state M
			dsram->sram[index][offset] = request->data;
			tsram->MESI[index] = MODIFIED;
			request->done = 1;
			printf("GAM PO\n");
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
			requests[core_index]->index = index;
			requests[core_index]->type = BUSRDX; // indicate BusRdX
			requests[core_index]->data = request->data;
			core_has_request[core_index] = 1;
			printf("LO TOV\n");
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
			requests[core_index]->index = index;
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
			requests[core_index]->index = index;
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
		//printf("flush dsrams_array[%d]=[%d][%d]=%x\n", i, index, offset, bus_data);
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
				//printf("curr_request->index = %d\n\n", curr_request->index);

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
				cachestall[curr_request->core_index] = 0;
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
			pr_req->data = dsrams_array[core_ind]->sram[curr_request->index][curr_request->offset]; // giving the core the data from cache
			//pr_req->data = dsrams_array[core_ind]->sram[3][3]; // giving the core the data from cache
			//printf("DATA: dsrams_array[%d]->sram[%d][%d]=%x\n", core_ind, curr_request->index, curr_request->offset, pr_req->data);

			if (tsrams_array[pr_req->core_index]->MESI[pr_req->index] == INVALID) tsrams_array[pr_req->core_index]->MESI[pr_req->index] = SHARED;
		}
		pr_req->done = 1; // telling the core that it can continue (use the data)
		pr_requests[core_ind] = NULL;
		free(curr_request);
	}
}


void bus_step() {
	int core_to_serve;
	if (bus_cmd == NO_CMD) {
		// code for setting a request and activating it
		core_to_serve = choose_core();

		if (core_to_serve == -1) return; // No transaction needed
		
		cachestall[core_to_serve] = 1;

		curr_request = requests[core_to_serve];
		//memcpy(curr_request, requests[core_to_serve], sizeof(BUS_REQ*));
		core_used_bus(core_to_serve); // update the priority
		//requests[core_to_serve] = NULL;
		core_has_request[core_to_serve] = 0;
		place_request_on_bus();
		requests[core_to_serve] = NULL;
	}

	bus_logic_before_snooping();


	for (int i = 0; i < 4; i++) { // all the cores are snooping
		core_i_snoop(i);
	}
	main_mem_snoop();

	bus_logic_after_snooping();

	check_if_req_fulfilled();
	for (int i = 0; i < 4; i++) {
		printf("tsrams_array[%d]->MESI[curr_request->index] = %d\n", i, tsrams_array[i]->MESI[3]);
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


