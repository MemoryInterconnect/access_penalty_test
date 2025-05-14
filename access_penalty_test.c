#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "check_mem_latency.h"

int main(int argc, char ** argv) {
    double local_mem_latency=0, meca_mem_latency=0;
    double temp, min, max, total_latency;
    void * local_buf = NULL;
    long test_size=0, stride=0;
    size_t page_size;
    int i, loop;

    if (argc < 4) {
        printf("Usage: %s [size] [stride] [loop count]\n", argv[0]);
        return 0;
    }

    test_size = atol(argv[1]);
    stride = atol(argv[2]);
    loop = atoi(argv[3]);

    page_size = getpagesize();
    local_buf = aligned_alloc(page_size, test_size);
    if (local_buf == NULL) {
	    printf("Error: %s\n", strerror(errno));
	    return -1;
    }
    
    printf("Local Memory Test\n");
    total_latency = 0;
    min = max = 0;
    for (i=0; i<loop; i++) {
	    temp = check_mem_latency(local_buf, test_size, stride);
	    printf ("%d: %.2lf\n", i+1, temp);
	    total_latency += temp;
	    if ( i == 0 ) min = max = temp;
	    if ( temp < min ) min = temp;
	    if ( temp > max ) max = temp;
    }

    local_mem_latency = (total_latency - min - max) / (loop-2);
    printf("Local Memory Latency: average = %lf usec\n", local_mem_latency/CLOCK_PER_USEC);


	return 0;
}
