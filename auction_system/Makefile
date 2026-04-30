# ─────────────────────────────────────────────────────────────
#  Online Auction System – Makefile
#  EGC 301P / Operating Systems Lab
# ─────────────────────────────────────────────────────────────

CC      = gcc
CFLAGS  = -Wall -Wextra -g -Iinclude
LDFLAGS = -lpthread -lrt

SRC_DIR = src
OBJ_DIR = obj

# ── Shared object files (compiled once, linked into all targets) ──
SHARED_SRC = \
    $(SRC_DIR)/logger.c    \
    $(SRC_DIR)/user_db.c   \
    $(SRC_DIR)/item_db.c   \
    $(SRC_DIR)/bid_engine.c \
    $(SRC_DIR)/ipc.c       \
    $(SRC_DIR)/auth.c      \
    $(SRC_DIR)/server.c

SHARED_OBJ = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SHARED_SRC))

# ── Targets ──
SERVER   = auction_server
CLIENT   = auction_client
.PHONY: all clean dirs run_server run_client

all: dirs $(SERVER) $(CLIENT)

# ── Shared objects ──
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# ── Server binary ──
$(SERVER): $(SHARED_OBJ) $(OBJ_DIR)/main_server.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "  Built $(SERVER)"

$(OBJ_DIR)/main_server.o: $(SRC_DIR)/main_server.c
	$(CC) $(CFLAGS) -c $< -o $@

# ── Client binary ──
$(CLIENT): $(OBJ_DIR)/main_client.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "  Built $(CLIENT)"

$(OBJ_DIR)/main_client.o: $(SRC_DIR)/main_client.c
	$(CC) $(CFLAGS) -c $< -o $@


# ── Helpers ──
dirs:
	@mkdir -p $(OBJ_DIR) data logs

clean:
	rm -rf $(OBJ_DIR) $(SERVER) $(CLIENT) data logs

run_server:
	./$(SERVER)

run_client:
	./$(CLIENT)