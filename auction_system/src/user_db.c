/*
 * user_db.c
 * Persistent user store with advisory read/write file locking (fcntl).
 * Satisfies: File Locking (4.2), Data Consistency (4.4)
 */
#include "user_db.h"
#include "logger.h"
#include <fcntl.h>
#include <sys/file.h>

static User  g_users[MAX_USERS];
static int   g_count = 0;

/* ── File locking helpers ── */
static void lock_file(int fd, int type) {
    struct flock fl = {0};
    fl.l_type   = type;   /* F_RDLCK or F_WRLCK */
    fl.l_whence = SEEK_SET;
    fl.l_start  = 0;
    fl.l_len    = 0;      /* whole file */
    if (fcntl(fd, F_SETLKW, &fl) == -1) {
        LOG_ERROR("fcntl lock failed: %s", strerror(errno));
    }
}

static void unlock_file(int fd) {
    struct flock fl = {0};
    fl.l_type   = F_UNLCK;
    fl.l_whence = SEEK_SET;
    fcntl(fd, F_SETLK, &fl);
}

/* ── Load users from disk (read lock) ── */
int userdb_load(void) {
    int fd = open(USERS_FILE, O_RDONLY | O_CREAT, 0644);
    if (fd < 0) {
        LOG_WARN("users.dat not found; starting fresh");
        return 0;
    }
    lock_file(fd, F_RDLCK);

    FILE *fp = fdopen(fd, "r");
    if (!fp) { close(fd); return -1; }

    g_count = 0;
    while (g_count < MAX_USERS &&
           fread(&g_users[g_count], sizeof(User), 1, fp) == 1) {
        g_count++;
    }
    unlock_file(fd);
    fclose(fp); /* also closes fd */
    /* Note: unlock happens automatically on close */
    LOG_INFO("Loaded %d users from disk", g_count);
    return g_count;
}

/* ── Save users to disk (write lock) ── */
int userdb_save(void) {
    int fd = open(USERS_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        LOG_ERROR("Cannot open users.dat for write: %s", strerror(errno));
        return -1;
    }
    lock_file(fd, F_WRLCK);

    FILE *fp = fdopen(fd, "w");
    if (!fp) { close(fd); return -1; }

    for (int i = 0; i < g_count; i++) {
        fwrite(&g_users[i], sizeof(User), 1, fp);
    }
    fflush(fp);
    unlock_file(fd);
    fclose(fp);
    return 0;
}

/* ── CRUD ── */
int userdb_add(const User *u) {
    if (g_count >= MAX_USERS) return -1;
    if (userdb_find_by_name(u->username)) return -2; /* duplicate */
    g_users[g_count] = *u;
    g_users[g_count].id = g_count + 1;
    g_count++;
    return userdb_save();
}

User *userdb_find_by_id(int id) {
    for (int i = 0; i < g_count; i++)
        if (g_users[i].id == id && g_users[i].active)
            return &g_users[i];
    return NULL;
}

User *userdb_find_by_name(const char *username) {
    for (int i = 0; i < g_count; i++)
        if (strcmp(g_users[i].username, username) == 0)
            return &g_users[i];
    return NULL;
}

int userdb_disable(int id) {
    User *u = userdb_find_by_id(id);
    if (!u) return -1;
    u->active = 0;
    return userdb_save();
}

int userdb_change_password(int id, const char *new_pass) {
    User *u = userdb_find_by_id(id);
    if (!u) return -1;
    strncpy(u->password, new_pass, MAX_PASSWORD - 1);
    u->need_pw_change = 0;
    return userdb_save();
}

int userdb_count(void) { return g_count; }

User *userdb_get_all(int *count) {
    *count = g_count;
    return g_users;
}