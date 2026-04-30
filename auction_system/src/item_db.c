/*
 * item_db.c
 * Persistent auction item store.
 * Uses fcntl advisory read/write locks on items.dat.
 * Satisfies: File Locking (4.2), Data Consistency (4.4)
 */
#include "item_db.h"
#include "logger.h"
#include <fcntl.h>

static Item g_items[MAX_ITEMS];
static int  g_count = 0;

/* ── Lock helpers (same pattern as user_db) ── */
static void lock_fd(int fd, int type) {
    struct flock fl = {0};
    fl.l_type   = type;
    fl.l_whence = SEEK_SET;
    if (fcntl(fd, F_SETLKW, &fl) == -1)
        perror("fcntl item lock");
}
static void unlock_fd(int fd) {
    struct flock fl = { .l_type = F_UNLCK };
    fcntl(fd, F_SETLK, &fl);
}

int itemdb_load(void) {
    int fd = open(ITEMS_FILE, O_RDONLY | O_CREAT, 0644);
    if (fd < 0) { return 0; }
    lock_fd(fd, F_RDLCK);
    FILE *fp = fdopen(fd, "r");
    if (!fp) { close(fd); return -1; }
    g_count = 0;
    while (g_count < MAX_ITEMS &&
           fread(&g_items[g_count], sizeof(Item), 1, fp) == 1)
        g_count++;
    unlock_fd(fd);
    fclose(fp);
    LOG_INFO("Loaded %d items from disk", g_count);
    return g_count;
}

int itemdb_save(void) {
    int fd = open(ITEMS_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    lock_fd(fd, F_WRLCK);
    FILE *fp = fdopen(fd, "w");
    if (!fp) { close(fd); return -1; }
    for (int i = 0; i < g_count; i++)
        fwrite(&g_items[i], sizeof(Item), 1, fp);
    fflush(fp);
    unlock_fd(fd);
    fclose(fp);
    return 0;
}

int itemdb_add(Item *item) {
    if (g_count >= MAX_ITEMS) return -1;
    item->id = g_count + 1;
    item->active = 1;
    item->current_price = item->starting_price;
    item->highest_bidder_id = -1;
    g_items[g_count++] = *item;
    return itemdb_save();
}

Item *itemdb_find(int id) {
    for (int i = 0; i < g_count; i++)
        if (g_items[i].id == id && g_items[i].active)
            return &g_items[i];
    return NULL;
}

int itemdb_update(const Item *item) {
    for (int i = 0; i < g_count; i++) {
        if (g_items[i].id == item->id) {
            g_items[i] = *item;
            return itemdb_save();
        }
    }
    return -1;
}

int itemdb_close(int id) {
    Item *it = itemdb_find(id);
    if (!it) return -1;
    it->status = ITEM_SOLD;
    it->active = 0;
    return itemdb_save();
}

Item *itemdb_get_active(int *count) {
    /* Return pointer into static array of items whose status == ACTIVE */
    static Item active_buf[MAX_ITEMS];
    int c = 0;
    time_t now = time(NULL);
    for (int i = 0; i < g_count; i++) {
        if (g_items[i].status == ITEM_ACTIVE && g_items[i].active) {
            if (g_items[i].end_time > 0 && g_items[i].end_time <= now) {
                g_items[i].status = ITEM_EXPIRED;
                itemdb_save();
                continue;
            }
            active_buf[c++] = g_items[i];
        }
    }
    *count = c;
    return active_buf;
}

Item *itemdb_get_all(int *count) {
    *count = g_count;
    return g_items;
}