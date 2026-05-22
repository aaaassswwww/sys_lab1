CC := gcc
CFLAGS := -O2 -Wall -Wextra -Wpedantic -std=c11
LDFLAGS :=
PTHREAD_FLAGS := -pthread
SRC_DIR := src
COMMON_OBJ := $(SRC_DIR)/common.o

TARGETS := poll_server epoll_server threadpool_server io_uring_server load_client

.PHONY: all clean

all: $(TARGETS)

$(COMMON_OBJ): $(SRC_DIR)/common.c $(SRC_DIR)/common.h
	$(CC) $(CFLAGS) -c -o $@ $(SRC_DIR)/common.c

poll_server: $(SRC_DIR)/poll_server.c $(COMMON_OBJ)
	$(CC) $(CFLAGS) -o $@ $(SRC_DIR)/poll_server.c $(COMMON_OBJ)

epoll_server: $(SRC_DIR)/epoll_server.c $(COMMON_OBJ)
	$(CC) $(CFLAGS) -o $@ $(SRC_DIR)/epoll_server.c $(COMMON_OBJ)

threadpool_server: $(SRC_DIR)/threadpool_server.c $(COMMON_OBJ)
	$(CC) $(CFLAGS) $(PTHREAD_FLAGS) -o $@ $(SRC_DIR)/threadpool_server.c $(COMMON_OBJ)

io_uring_server: $(SRC_DIR)/io_uring_server.c $(COMMON_OBJ)
	$(CC) $(CFLAGS) -o $@ $(SRC_DIR)/io_uring_server.c $(COMMON_OBJ)

load_client: $(SRC_DIR)/load_client.c $(COMMON_OBJ)
	$(CC) $(CFLAGS) $(PTHREAD_FLAGS) -o $@ $(SRC_DIR)/load_client.c $(COMMON_OBJ)

clean:
	rm -f $(TARGETS) $(SRC_DIR)/*.o
