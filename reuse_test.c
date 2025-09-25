// rdbench_reuse_distance.c
// Build:  gcc -O2 -march=native -Wall -Wextra -o rdbench_reuse_distance rdbench_reuse_distance.c
// Usage:  ./rdbench_reuse_distance --reuse-bytes 65536 --iters 1000000 [--array-bytes 134217728] [--line-bytes 64] [--warmup 1]
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <errno.h>
//#include <x86intrin.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#ifndef likely
#define likely(x)   __builtin_expect(!!(x),1)
#endif
#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x),0)
#endif

typedef struct {
    size_t array_bytes;   // 전체 배열 크기
    size_t reuse_bytes;   // 재사용 거리(같은 라인을 다시 만날 때까지의 바이트)
    size_t line_bytes;    // 캐시라인 크기 가정(기본 64)
    uint64_t iters;       // 측정 반복 횟수(접근 횟수)
    int warmup;           // 측정 전 워밍업 여부
    int use_meca;         // MECA 메모리 사용 여부
} config_t;

static void die(const char* msg){
    fprintf(stderr, "Error: %s\n", msg);
    exit(1);
}

static int parse_arg_i(const char* s, long long* out){
    char* end=NULL;
    errno=0;
    long long v = strtoll(s, &end, 10);
    if(errno!=0 || end==s) return -1;
    *out=v; return 0;
}
static int parse_arg_z(const char* s, size_t* out){
    char* end=NULL;
    errno=0;
    unsigned long long v = strtoull(s, &end, 10);
    if(errno!=0 || end==s) return -1;
    *out=(size_t)v; return 0;
}
static void parse_args(int argc, char** argv, config_t* cfg){
    // defaults
    cfg->array_bytes = 128ULL<<20; // 128 MiB
    cfg->reuse_bytes = 64ULL<<10;  // 64 KiB
    cfg->line_bytes  = 64;
    cfg->iters       = 1000000ULL;
    cfg->warmup      = 1;
    cfg->use_meca    = 0;

    for(int i=1;i<argc;i++){
        if(strcmp(argv[i],"--array-bytes")==0 && i+1<argc){
            if(parse_arg_z(argv[++i], &cfg->array_bytes)) die("bad --array-bytes");
        } else if(strcmp(argv[i],"--reuse-bytes")==0 && i+1<argc){
            if(parse_arg_z(argv[++i], &cfg->reuse_bytes)) die("bad --reuse-bytes");
        } else if(strcmp(argv[i],"--line-bytes")==0 && i+1<argc){
            if(parse_arg_z(argv[++i], &cfg->line_bytes)) die("bad --line-bytes");
        } else if(strcmp(argv[i],"--iters")==0 && i+1<argc){
            long long t; if(parse_arg_i(argv[++i], &t) || t<=0) die("bad --iters");
            cfg->iters = (uint64_t)t;
        } else if(strcmp(argv[i],"--warmup")==0 && i+1<argc){
            long long t; if(parse_arg_i(argv[++i], &t)) die("bad --warmup");
            cfg->warmup = (int)t;
        } else if(strcmp(argv[i],"--use_meca")==0 && i+1<argc){
            long long t; if(parse_arg_i(argv[++i], &t)) die("bad --use_meca");
            cfg->use_meca = (int)t;
        } else if(strcmp(argv[i],"--help")==0){
            printf("Usage: %s --reuse-bytes N [--array-bytes B] [--line-bytes L] [--iters I] [--warmup 0|1] [--use_meca 0|1]\n", argv[0]);
            exit(0);
        } else {
            fprintf(stderr, "Unknown arg: %s\n", argv[i]);
            exit(1);
        }
    }
    if(cfg->line_bytes==0 || cfg->reuse_bytes<cfg->line_bytes){
        die("reuse-bytes must be >= line-bytes and non-zero");
    }
    if(cfg->array_bytes < cfg->reuse_bytes*2){
        // 배열이 너무 작으면 캐시 효과가 왜곡될 수 있음
        cfg->array_bytes = cfg->reuse_bytes*4;
    }
}

#if 0
// x86_64 rdtsc helpers
#if defined(__x86_64__) || defined(_M_X64)
static inline uint64_t rdtsc_begin(void){
    unsigned int aux;
    _mm_mfence();
    uint64_t t = __rdtscp(&aux); // serialize
    _mm_lfence();
    return t;
}
static inline uint64_t rdtsc_end(void){
    unsigned int aux;
    _mm_lfence();
    uint64_t t = __rdtscp(&aux);
    _mm_mfence();
    return t;
}
#define USE_TSC 1
#else
#define USE_TSC 0
#endif
#endif

#define USE_TSC 0

static inline uint64_t nsec_now(void){
    struct timespec ts;
#if defined(CLOCK_MONOTONIC_RAW)
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
#else
    clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
    return (uint64_t)ts.tv_sec*1000000000ull + (uint64_t)ts.tv_nsec;
}

// 정확한 reuse distance를 위한 접근 패턴 생성
// reuse_slots개의 서로 다른 cache line을 순차적으로 접근한 후 처음으로 돌아감
static void build_reuse_pattern(size_t* idx, size_t nslots, size_t reuse_slots){
    // reuse_slots보다 큰 배열이어야 함
    if(nslots < reuse_slots + 1){
        fprintf(stderr, "Array too small for requested reuse distance\n");
        exit(1);
    }

    // 0 -> 1 -> 2 -> ... -> reuse_slots-1 -> 0 패턴 생성
    for(size_t i = 0; i < reuse_slots; i++){
        idx[i] = (i + 1) % reuse_slots;
    }

    // 나머지 슬롯들은 사용하지 않음 (정확한 reuse distance를 위해)
    for(size_t i = reuse_slots; i < nslots; i++){
        idx[i] = i; // 자기 자신을 가리켜 사용하지 않음을 표시
    }
}

#define MECA_DEV "/dev/mem"
#define MECA_OFFSET 0x200000000UL

int main(int argc, char** argv){
    config_t cfg;
    parse_args(argc, argv, &cfg);

    const size_t line = cfg.line_bytes;
    const size_t slots = cfg.array_bytes / line;
    const size_t reuse_slots = cfg.reuse_bytes / line;
    if(reuse_slots==0) die("reuse-bytes too small relative to line-bytes");

    // 정확한 reuse distance 테스트를 위한 패턴 구성
    // reuse_slots개의 서로 다른 cache line을 순환 접근
    size_t* next_idx = (size_t*)aligned_alloc(line, slots * sizeof(size_t));
    if(!next_idx) die("alloc next_idx failed");
    build_reuse_pattern(next_idx, slots, reuse_slots);

    // 실제 데이터(읽기 전용)
    uint8_t* buf = NULL;
    int mem_fd = -1;

    if(cfg.use_meca){
        // MECA 메모리 사용 - /dev/mem을 통해 MECA 영역에 접근
        mem_fd = open(MECA_DEV, O_RDWR | O_SYNC);
        if(mem_fd == -1) die("failed to open /dev/mem (need root privileges)");

        buf = (uint8_t*)mmap(NULL, slots * line, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, MECA_OFFSET);
        if(buf == MAP_FAILED) die("mmap failed on MECA memory region");
    } else {
        // 일반 메모리 할당
        buf = aligned_alloc(line, slots * line);
        if(!buf) die("alloc buf failed");
    }

    // 터치해서 물리 페이지 확보
    for(size_t i=0;i<slots*line;i+=4096) buf[i]= (uint8_t)(i);

    // 워밍업
    volatile uint64_t sink=0;
    if(cfg.warmup){
        size_t p=0;
        for(uint64_t i=0;i<cfg.iters/10+1000;i++){
            // 각 접근에서 해당 라인의 첫 바이트만 읽어도 캐시라인 로드됨
            sink += buf[p*line];
            p = next_idx[p];
        }
    }

    uint64_t begin=0, end=0;
/*    if(USE_TSC){
        begin = rdtsc_begin();
        size_t p=0;
        for(uint64_t i=0;i<cfg.iters;i++){
            sink += buf[p*line];
            p = next_idx[p];
        }
        end = rdtsc_end();
        double avg_cycles = (double)(end - begin) / (double)cfg.iters;
        // CSV: reuse_bytes,avg_cycles,iters
        printf("%zu,%.3f,%llu\n", cfg.reuse_bytes, avg_cycles, (unsigned long long)cfg.iters);
    } else */
	{
        begin = nsec_now();
        size_t p=0;
        for(uint64_t i=0;i<cfg.iters;i++){
            sink += buf[p*line];
            p = next_idx[p];
        }
        end = nsec_now();
        double avg_ns = (double)(end - begin) / (double)cfg.iters;
        // CSV: reuse_bytes,avg_ns,iters
        printf("%zu bytes %.3f ns %llu times\n", cfg.reuse_bytes, avg_ns, (unsigned long long)cfg.iters);
    }

    // anti-opt
    if(sink==42) fprintf(stderr,".\n");
    free((void*)next_idx);

    // 메모리 정리
    if(cfg.use_meca){
        munmap(buf, slots * line);
        close(mem_fd);
    } else {
        free((void*)buf);
    }
    return 0;
}

