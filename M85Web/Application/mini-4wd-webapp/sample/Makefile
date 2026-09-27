CC = gcc
CFLAGS = -Wall -Wextra -O2 -pthread -D_GNU_SOURCE
TARGET = mini4wd_server

SRCS = main.c controller.c http_server.c
OBJS = $(SRCS:.c=.o)

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)
