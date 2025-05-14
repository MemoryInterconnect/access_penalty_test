#define CLOCK_PER_USEC 100 //100MHz
void prepare_mem_for_latency_test(void *buf, long size, long stride);
double check_mem_latency(void *buf, long size, long stride);
