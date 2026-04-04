CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -std=gnu99
LDFLAGS = -lm
TARGET  = sdfq_sim

SRCS    = main.c generator.c scheduler.c
OBJS    = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c task.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	del /Q $(OBJS) $(TARGET).exe 2>nul || rm -f $(OBJS) $(TARGET)

.PHONY: all clean
