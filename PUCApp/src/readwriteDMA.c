#include "readwriteDMA.h"

static void dma_reset(volatile uint32_t *base){
	base[7] = 0x0200000A;
	return;
}
static void dma_request(volatile uint32_t *base,unsigned long pa, unsigned long ha, unsigned long size, unsigned long next, unsigned int bar_no, int block){
	uint32_t pa_h;
	uint32_t pa_l;
	uint32_t ha_h;
	uint32_t ha_l;
	uint32_t length;
	uint32_t control;
	uint32_t next_bda_h;
	uint32_t next_bda_l;

	pa_h = (pa >> 32);
	pa_l = pa;
	ha_h = (ha >> 32);
	ha_l = ha;
	length = size;
	control = 0x03008000 | (bar_no << 16);
	next_bda_h = (next >> 32);
	next_bda_l = next;

	base[0] = pa_h;
	base[1] = pa_l;
	base[2] = ha_h;
	base[3] = ha_l;
	base[4] = next_bda_h;
	base[5] = next_bda_l;
	base[6] = length;
	base[7] = control;	// control is written at the end, starts DMA
	
	if(block)
		sleep(5);//TODO:wait for the finished status

	return;
}
/*Write DMA*/
static void writeDMA(void *bar0, unsigned long ha, unsigned long pa, unsigned long next, unsigned long size, unsigned int bar_no, int block){

	uint32_t *bar_zero = (uint32_t*)bar0;
	const unsigned int BASE_DMA_DOWN = (0x50 >> 2);


	// TODO: add inc to control word
	// TODO: add lastDescriptor to control word, based on 'next'
	//

	dma_reset(bar_zero+BASE_DMA_DOWN);

	// Send a DMA transfer

	dma_request(bar0+BASE_DMA_DOWN,pa,ha,size,next,bar_no,block);

	return;
}
/*Read DMA*/
static void readDMA(uint32_t *bar0, unsigned long ha, unsigned long pa, unsigned long next, unsigned long size, unsigned int bar_no, int block)
{
	const unsigned int BASE_DMA_UP = (0x2C >> 2);

	// TODO: add inc to control word
	// TODO: add lastDescriptor to control word, based on 'next'

	dma_reset(bar0+BASE_DMA_UP);

	// Send a DMA transfer
	dma_request(bar0+BASE_DMA_UP,pa,ha,size,next,bar_no,block);
	return;
}

void DMAKernelMemoryRead(uint32_t *bar0, uint32_t *bar1, uint64_t *bar2, pd_kmem_t *km, const unsigned long test_len, void *kernel_memory,int block){
	uint32_t *ptr = (uint32_t*)kernel_memory;
	unsigned int bar_no = 0x0;

	if (bar1 != 0) 
		bar_no = 0x1;
	else if (bar2 != 0) 
		bar_no = 0x2;
	
	printf("Fill buffer with zeros\n");
	memset(ptr, 3, test_len);
	/*TODO:check kernel_memory.pa*/
	readDMA(bar0,km->pa,0x00000000, 0x00000000, test_len, bar_no, block);
	return;
}
void DMAKernelMemoryWrite(uint32_t *bar0, uint32_t *bar1, uint64_t *bar2, pd_kmem_t *km, const unsigned long test_len, void *kernel_memory, int block){
	int i=0;
	uint32_t *ptr = (uint32_t*)kernel_memory;
	unsigned int bar_no = 0x0;
	if (bar1 != 0) 
		bar_no = 0x1;
	else if (bar2 != 0) 
		bar_no = 0x2;

	printf("Fill buffer with a pattern\n");
	// fill with pattern
	for(i=0;i<(test_len >> 2);i++) {
		if ((i & 0x00000001) == 0)
			ptr[i] = i;
		else
			ptr[i] = 0xaaaa5555;
	}

	writeDMA(bar0, km->pa, 0x00000000, 0x00000000, test_len, bar_no, block);

	return;
}
void DMAKernelClearBuffer(uint32_t *bar0, uint32_t *bar1, const unsigned long test_len){
	// Clear buffer
	printf("Clear FPGA area with zeros\n");
	if (bar1 != 0) {
		memset(bar1, 0, test_len );
	}
	/*else if (bar2 != 0) {
		cout << "Press key to continue..." << endl;
		getchar();
		memset(bar2, 0, test_len);
	}*/
	return;
}
	
