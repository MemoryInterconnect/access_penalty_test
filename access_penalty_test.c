#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include "check_mem_latency.h"

#define MECA_DEV "/dev/mem"
#define MECA_OFFSET 0x200000000UL

int main(int argc, char ** argv) {
    double local_mem_latency=0, meca_mem_latency=0;
    double temp, min, max, total_latency;
    void * local_buf = NULL, *meca_buf = NULL;
    long test_size=0, stride=0;
    size_t page_size;
    int i, loop;
    int fd;

    if (argc < 4) {
        printf("Usage: %s [size] [stride] [loop count]\n", argv[0]);
        return 0;
    }

    test_size = atol(argv[1]);
    stride = atol(argv[2]);
    loop = atoi(argv[3]);

    //Allocte Local memory for latency test
    page_size = getpagesize();
    local_buf = aligned_alloc(page_size, test_size);
    if (local_buf == NULL) {
	    printf("Local Memory allocation error: %s\n", strerror(errno));
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
    printf("Local Memory Latency: average = %.4lf usec\n", local_mem_latency/CLOCK_PER_USEC);

    free (local_buf);
    //end of Local memory test
    
    //Allocate MECA memory for latency test
    fd = open("/dev/mem", O_RDWR);
    if ( fd < 0 ) {
	printf("MECA device open error: %s\n", strerror(errno));
	return -1;
    }

    meca_buf = mmap(0, test_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, MECA_OFFSET);
    if ( meca_buf == NULL) {
	    printf("MECA Memory allocation error: %s\n", strerror(errno));
	    goto out;
    }

    printf("MECA Memory Test\n");
    total_latency = 0;
    min = max = 0;
    for (i=0; i<loop; i++) {
	    temp = check_mem_latency(meca_buf, test_size, 8);
	    printf ("%d: %.2lf\n", i+1, temp);
	    total_latency += temp;
	    if ( i == 0 ) min = max = temp;
	    if ( temp < min ) min = temp;
	    if ( temp > max ) max = temp;
    }

    meca_mem_latency = (total_latency - min - max) / (loop-2);
    printf("MECA Memory Latency: average = %.4lf usec\n", meca_mem_latency/CLOCK_PER_USEC);

    munmap(meca_buf, test_size);

//Calculate Access Penalty
    printf("Access Penalty(%%) = (meca_mem_latency - local_mem_latency) / local_mem_latency * 100\n");
    printf("Access Penalty: %lf %%\n", (meca_mem_latency - local_mem_latency)/local_mem_latency*100);

out:
    close(fd);

    return 0;
}
