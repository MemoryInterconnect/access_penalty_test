CC=riscv64-buildroot-linux-gnu-gcc
#CC = gcc
CFLAGS = -Wall -Wextra -O2 -DUSE_RDCYCLE
LDFLAGS = -lm -static
TARGET = access_penalty_test
SRC = access_penalty_test.c check_mem_latency.c
OBJ = $(SRC:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

