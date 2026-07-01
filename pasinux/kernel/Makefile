CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -g
LDFLAGS ?=
TARGET := kernel_sim
OBJS := kernel.o mm.o scheduler.o driver.o ipc.o

.PHONY: all clean run syntax

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

syntax:
	$(CC) $(CFLAGS) -fsyntax-only kernel.c mm.c scheduler.c driver.c ipc.c

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET) $(TARGET).exe
