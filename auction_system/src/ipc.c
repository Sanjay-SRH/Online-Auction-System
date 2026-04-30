/*
 * ipc.c
 * Inter-Process Communication.
 *
 * Satisfies IPC (4.6):
 *   - Shared memory      → live scoreboard readable by any process
 *   - Signals            → SIGTERM / SIGINT graceful shutdown
 */
#include "ipc.h"
#include "logger.h"
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <signal.h>
#include <fcntl.h>

/* ── Shared memory ── */
static int          g_shm_id = -1;
static SharedBoard *g_board  = NULL;

int ipc_shm_init(void) {
    key_t key = ftok(".", 'A');
    g_shm_id  = shmget(key, sizeof(SharedBoard), IPC_CREAT | 0666);
    if (g_shm_id < 0) {
        LOG_ERROR("shmget failed: %s", strerror(errno));
        return -1;
    }
    g_board = (SharedBoard *)shmat(g_shm_id, NULL, 0);
    if (g_board == (SharedBoard *)-1) {
        LOG_ERROR("shmat failed: %s", strerror(errno));
        g_board = NULL;
        return -1;
    }
    memset(g_board, 0, sizeof(SharedBoard));
    LOG_INFO("Shared memory board initialized (id=%d)", g_shm_id);
    return 0;
}

SharedBoard *ipc_shm_get(void) { return g_board; }

void ipc_shm_update(int item_id, double price, int bid_count, int top_bidder) {
    if (!g_board) return;
    /* Find or create slot */
    for (int i = 0; i < g_board->count; i++) {
        if (g_board->items[i].item_id == item_id) {
            g_board->items[i].current_price     = price;
            g_board->items[i].bid_count         = bid_count;
            g_board->items[i].highest_bidder_id = top_bidder;
            return;
        }
    }
    if (g_board->count < MAX_ITEMS) {
        int idx = g_board->count++;
        g_board->items[idx].item_id            = item_id;
        g_board->items[idx].current_price      = price;
        g_board->items[idx].bid_count          = bid_count;
        g_board->items[idx].highest_bidder_id  = top_bidder;
    }
}

void ipc_shm_destroy(void) {
    if (g_board) shmdt(g_board);
    if (g_shm_id >= 0) shmctl(g_shm_id, IPC_RMID, NULL);
    LOG_INFO("Shared memory destroyed");
}

/* ── Signals ── */
static volatile sig_atomic_t g_running = 1;

static void sig_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        g_running = 0;
        LOG_INFO("Signal %d received – shutting down gracefully", sig);
    } else if (sig == SIGUSR1) {
        LOG_INFO("SIGUSR1: force-refresh live board requested");
    }
}

void ipc_signal_setup(void) {
    struct sigaction sa = {0};
    sa.sa_handler = sig_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);
    LOG_INFO("Signal handlers installed (SIGINT, SIGTERM, SIGUSR1)");
}