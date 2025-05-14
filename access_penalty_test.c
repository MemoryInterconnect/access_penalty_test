#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "check_mem_latency.h"

int main(int argc, char ** argv) {
    double local_mem_latency=0, meca_mem_latency=0;
    double temp;
    void * local_buf = NULL;
    long test_size=0, stride=0;
    size_t page_size;

    if (argc < 3) {
        printf("Usage: %s [size] [stride]", argv[0]);
        return 0;
    }

    test_size = atoi(argv[1]);
    stride = atoi(argv[2]);

    page_size = getpagesize();
    local_buf = aligned_alloc(page_size, test_size);
    if (local_buf == NULL) {
	    printf("Error: %s\n", strerror(errno));
	    return -1;
    }
    
    temp = check_mem_latency(local_buf, test_size, stride);


	return 0;
}
