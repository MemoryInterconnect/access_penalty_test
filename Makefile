#CC=riscv64-buildroot-linux-gnu-gcc
CC = gcc
CFLAGS = -Wall -g -Wextra -O2 #-DUSE_RDCYCLE
LDFLAGS = -lm -static
TARGET1 = access_penalty_test 
TARGET2 = reuse_test
SRC1 = access_penalty_test.c check_mem_latency.c 
SRC2 = reuse_test.c
OBJ = $(SRC1:.c=.o)

all: $(TARGET1) $(TARGET2)

$(TARGET1): $(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET1) $(OBJ) $(LDFLAGS)

$(TARGET2): $(SRC2)
	$(CC) $(CFLAGS) -march=native -o $(TARGET2) $(SRC2)


%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET1) $(TARGET2)

