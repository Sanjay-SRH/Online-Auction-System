/*
 * bid_engine.c
 * Core bidding logic.
 *
 * Concurrency control (4.3):
 *   - pthread_mutex  → protects in-memory bid array and current_price update
 *   - POSIX semaphore → limits simultaneous bidders per item (seat limit)
 *
 * Data consistency (4.4):
 *   - Mutex held across price check + update → prevents race conditions
 *   - fcntl write-lock on bids.dat         → prevents dirty reads on disk
 *
 * File locking (4.2):
 *   - F_WRLCK when appending bids
 */
#include "bid_engine.h"
#include "item_db.h"
#include "logger.h"
#include "ipc.h"

#include <pthread.h>
#include <semaphore.h>
#include <fcntl.h>

/* ── In-memory bid store ── */
static Bid   g_bids[MAX_BIDS];
static int   g_count = 0;

/* ── Mutex: protects g_bids[] and item's current_price ── */
static pthread_mutex_t g_bid_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ── Named semaphore: limit concurrent bidders per item to 10 ── */
#define SEM_NAME  "/auction_bid_sem"
#define SEM_LIMIT 10
static sem_t *g_sem = NULL;

/* ── File-lock helper ── */
static void flock_write(int fd) {
    struct flock fl = { .l_type = F_WRLCK, .l_whence = SEEK_SET };
    fcntl(fd, F_SETLKW, &fl);
}
static void flock_unlock(int fd) {
    struct flock fl = { .l_type = F_UNLCK, .l_whence = SEEK_SET };
    fcntl(fd, F_SETLK, &fl);
}

/* ─────────────────────────────────────────────── */
int bid_load(void) {
    /* Initialize semaphore */
    sem_unlink(SEM_NAME);
    g_sem = sem_open(SEM_NAME, O_CREAT, 0644, SEM_LIMIT);
    if (g_sem == SEM_FAILED) {
        LOG_ERROR("sem_open failed: %s", strerror(errno));
        return -1;
    }

    int fd = open(BIDS_FILE, O_RDONLY | O_CREAT, 0644);
    if (fd < 0) return 0;
    FILE *fp = fdopen(fd, "r");
    if (!fp) { close(fd); return -1; }
    g_count = 0;
    while (g_count < MAX_BIDS &&
           fread(&g_bids[g_count], sizeof(Bid), 1, fp) == 1)
        g_count++;
    fclose(fp);
    LOG_INFO("Loaded %d bids from disk", g_count);
    return g_count;
}

int bid_save(void) {
    int fd = open(BIDS_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    flock_write(fd);
    FILE *fp = fdopen(fd, "w");
    if (!fp) { close(fd); return -1; }
    for (int i = 0; i < g_count; i++)
        fwrite(&g_bids[i], sizeof(Bid), 1, fp);
    fflush(fp);
    flock_unlock(fd);
    fclose(fp);
    return 0;
}

/* ─────────────────────────────────────────────── */
int bid_place(int item_id, int bidder_id, double amount) {
    /* 1. Semaphore: limit concurrent access (seat limit) */
    if (g_sem && sem_trywait(g_sem) != 0) {
        LOG_WARN("Bid rejected: system at capacity (semaphore)");
        return -3;  /* server busy */
    }

    /* 2. Mutex: serialise the critical section */
    pthread_mutex_lock(&g_bid_mutex);

    int ret = 0;

    Item *item = itemdb_find(item_id);
    if (!item) {
        LOG_WARN("Bid on unknown item %d", item_id);
        ret = -1;
        goto done;
    }
    if (item->status != ITEM_ACTIVE) {
        LOG_WARN("Bid on inactive item %d", item_id);
        ret = -2;
        goto done;
    }
    /* Check auction end time */
    if (item->end_time > 0 && time(NULL) >= item->end_time) {
        item->status = ITEM_EXPIRED;
        itemdb_update(item);
        ret = -4;
        goto done;
    }
    /* Price validation (prevents race-condition lost update) */
    double min_valid = item->current_price + item->min_increment;
    if (amount < min_valid) {
        LOG_WARN("Bid %.2f too low; minimum is %.2f", amount, min_valid);
        ret = -5;
        goto done;
    }
    if (g_count >= MAX_BIDS) {
        ret = -6;
        goto done;
    }

    /* Record bid */
    Bid b;
    b.id        = g_count + 1;
    b.item_id   = item_id;
    b.bidder_id = bidder_id;
    b.amount    = amount;
    b.timestamp = time(NULL);
    g_bids[g_count++] = b;

    /* Update item's current price atomically inside the mutex */
    item->current_price     = amount;
    item->highest_bidder_id = bidder_id;
    itemdb_update(item);

    /* Persist bids (write-lock inside bid_save) */
    bid_save();

    /* Notify via shared memory (live board) */
    ipc_shm_update(item_id, amount, g_count, bidder_id);


    LOG_INFO("Bid placed: item=%d bidder=%d amount=%.2f", item_id, bidder_id, amount);

done:
    pthread_mutex_unlock(&g_bid_mutex);
    if (g_sem) sem_post(g_sem);
    return ret;
}

Bid *bid_get_for_item(int item_id, int *count) {
    static Bid buf[MAX_BIDS];
    int c = 0;
    pthread_mutex_lock(&g_bid_mutex);
    for (int i = 0; i < g_count; i++)
        if (g_bids[i].item_id == item_id)
            buf[c++] = g_bids[i];
    pthread_mutex_unlock(&g_bid_mutex);
    *count = c;
    return buf;
}