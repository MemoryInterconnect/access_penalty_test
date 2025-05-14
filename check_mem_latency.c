#include <stdint.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include "check_mem_latency.h"

// Pointer chasing macros to force the loop to be unwound
#define CHASE1(x) ((uintptr_t *)*x)
#define CHASE2(x) CHASE1(CHASE1(x))
#define CHASE4(x) CHASE2(CHASE2(x))
#define CHASE8(x) CHASE4(CHASE4(x))
#define CHASE16(x) CHASE8(CHASE8(x))
#define CHASE32(x) CHASE16(CHASE16(x))
#define CHASE64(x) CHASE32(CHASE32(x))
#define CHASE128(x) CHASE64(CHASE64(x))
#define CHASE256(x) CHASE128(CHASE128(x))
#define CHASE512(x) CHASE256(CHASE256(x))
#define CHASE1024(x) CHASE512(CHASE512(x))
#define CHASE_STEPS 1024

#ifdef USE_RDCYCLE
static uintptr_t rdcycle()
{
    uintptr_t out;
    __asm__ __volatile__("rdcycle %0":"=r"(out));
    return out;
}
#else
#include <sys/time.h>
static inline uintptr_t rdcycle()
{
        struct timeval tp;
        struct timezone tzp;
        double usec;
        gettimeofday(&tp,&tzp);
        usec = tp.tv_sec*1000000 + tp.tv_usec;
        return usec * CLOCK_PER_USEC;
}
#endif

uintptr_t *chase(uintptr_t * x, long *cycles)
{
    uintptr_t start = rdcycle();
    asm volatile ("":::"memory");
    uintptr_t *out = CHASE1024(x);
    asm volatile ("":::"memory");
    uintptr_t end = rdcycle();
    *cycles = end - start;
    return out;
}

#ifndef TEST_SIZE
#define TEST_SIZE (16*1024*1024)
#endif

// To prevent homonyms on a RISC-V for a VIPT system with virtual memory,
// there are never any set collisions under the 4kB MMU page-size.
// Therefore, you if accesss memory in 4kB stride, it's the same as touching
// the memory with unit stride.
#define STRIDE 4096


double check_mem_latency(void *buf, long size, long stride)
{

    long test_size;
    long test_range = size;
    long ways = 4;		//cache ways
    uintptr_t *bigarray = (uintptr_t *) buf;;
    long i, j, n, delta;
    long sum, sum2;
    uintptr_t *x = &bigarray[0];


    test_size = test_range;

    // Create a pointer loop
    i = 0;
    do {

	j = (i + stride) & (test_size - 1);

	bigarray[i / sizeof(uintptr_t)] =
	    (uintptr_t) & bigarray[j / sizeof(uintptr_t)];
	i = j;
    } while (i != 0);

    // We need to chase the point test_size/STRIDE steps to exercise the loop.
    // Each invocation of chase performs CHASE_STEPS, so round-up the calls.
    // To warm a cache with random replacement, you need to walk it 'ways' times.
    n = (((test_size / stride) * ways + (CHASE_STEPS - 1)) / CHASE_STEPS);
    if (n < 5)
	n = 5;			// enough to compute variance to within 50% = 1/sqrt(n-1)

    // Warm the cache test
    for (i = 0; i < n; ++i)
	x = chase(x, &delta);

    // Perform iterations for real
    sum = 0;
    sum2 = 0;
    for (i = 0; i < n; ++i) {
	x = chase(x, &delta);
	sum += delta;
	sum2 += delta * delta;
    }

    double mean = sum / (1.0 * n * CHASE_STEPS);
    // This is tricky. The sum2 and sum are actually measuring the random variable
    // which is a CHASE_STEPS* sum of the real random variable. To find the variance
    // of the underlying distribution, we need to multiply by sqrt(CHASE_STEPS).
    // We also need to divide by CHASE_STEPS to scale the result.
    double varDelta = (1.0 * sum2 - 1.0 * sum * sum / n) / (n - 1);
    double var = varDelta / sqrt(CHASE_STEPS);
//    printf("%ld %.3lf %.3lf %ld\n", test_size, mean, sqrt(var), n);

    // return mean latency clocks
    return mean;
}
