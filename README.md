<div align="center">

# 🔨 Online Auction System

**A Multi-Threaded Client-Server Auction Platform**

*EGC 301P – Operating Systems Laboratory Mini Project*

![Platform](https://img.shields.io/badge/Platform-Linux-blue?style=flat-square&logo=linux)
![Language](https://img.shields.io/badge/Language-C%20(POSIX)-orange?style=flat-square&logo=c)
![Threads](https://img.shields.io/badge/Threads-pthreads-green?style=flat-square)
![Sockets](https://img.shields.io/badge/Sockets-TCP%2FIP-purple?style=flat-square)
![License](https://img.shields.io/badge/License-Academic-red?style=flat-square)

</div>

---

## 🎯 Problem Statement

Traditional auction systems are either manual, error-prone, or rely on centralised web infrastructure that is difficult to inspect for OS-level behaviour. This project designs and implements a fully functional **Online Auction System from first principles**, using only **POSIX system calls and C standard libraries** — no high-level frameworks.

The system supports three distinct user roles — **Administrator**, **Auctioneer**, and **Bidder** — each with precisely controlled permissions. Multiple clients connect simultaneously, place competing bids, and observe a consistent, up-to-date auction state — all without data corruption or race conditions.

**Core challenges addressed:**

- Concurrent bid placement by multiple clients without lost-update anomalies
- Persistent storage of users, items, and bids across server restarts
- Safe multi-process file access through advisory locking
- Role-enforced access control preventing privilege escalation
- Real-time shared-memory scoreboard readable by any process without a network round-trip

---

## 📌 Project Overview

The Online Auction System is a Linux-based, multi-threaded client-server application written in C. Users connect over TCP, authenticate with a username/password, and are routed to a role-specific interactive dashboard. All persistent data is stored in binary flat files with `fcntl` advisory locks ensuring safe concurrent access.

### Features by Role

#### 🔴 Administrator
| Feature | Description |
|---|---|
| User Management | Add, list, and disable user accounts |
| Auction Control | Force-close any active auction regardless of owner |
| Item Visibility | View all items across all statuses (Pending / Active / Sold / Expired) |
| Account | Change own password |

#### 🔵 Auctioneer
| Feature | Description |
|---|---|
| Create Items | Add auction items with starting price and minimum bid increment |
| Start Auction | Activate a pending item with a configurable time limit |
| Edit Items | Update price/increment of a pending item before it goes live |
| Manage | Remove pending items, close own active auctions |
| View Bids | See full bid history for any item |

#### 🟢 Bidder
| Feature | Description |
|---|---|
| Browse | View active auctions with live time-remaining countdown |
| Bid | Place bids subject to minimum-increment enforcement |
| History | View personal bid history with Won / Lost / Ongoing status |
| Security | First-login forced password change |

---

## 🏗 System Architecture

The system follows a **client-server architecture** over TCP/IP. The server spawns one detached POSIX thread per accepted connection. All shared in-memory state is protected by a `pthread_mutex`. Disk state is protected by `fcntl` write/read locks. A POSIX shared-memory segment holds a live scoreboard readable by any process without a network call.

```
┌─────────────────────────────────────────────────────────────────┐
│                        SERVER PROCESS                           │
│                                                                 │
│   TCP :9090                                                     │
│   accept() loop ─────┬──────────────────────────────┐          │
│                      │                              │          │
│              [Thread A]                      [Thread B]        │
│              Client 1                        Client 2          │
│                      │                              │          │
│             ┌────────┴──────────────────────────────┘          │
│             │  Shared resources (mutex-protected)              │
│             │  ├── user_db   (users.dat  + fcntl lock)         │
│             │  ├── item_db   (items.dat  + fcntl lock)         │
│             │  └── bid_engine (bids.dat + mutex + semaphore)   │
│             │                                                  │
│             └──────► Shared Memory (live scoreboard)           │
└─────────────────────────────────────────────────────────────────┘

CLIENT PROCESS              CLIENT PROCESS
auction_client   ◄────────► auction_client
  │ TCP socket                │ TCP socket
  └── Interactive CLI         └── Interactive CLI
```

---

## ⚙️ OS Concepts Implemented

### 4.1 Role-Based Authorization

Four privilege levels are defined as a C enum in `common.h`. The role hierarchy assigns **lower numeric values to higher privileges** so that a single integer comparison grants or denies access.

```c
typedef enum {
    ROLE_ADMIN      = 0,   /* highest privilege */
    ROLE_AUCTIONEER = 1,
    ROLE_BIDDER     = 2,
    ROLE_GUEST      = 3    /* lowest privilege  */
} Role;

/* Returns 1 if user's role is high enough */
int auth_check_role(const User *user, Role required) {
    if (!user) return 0;
    return user->role <= required;
}
```

| Mechanism | Detail |
|---|---|
| Login guard | `auth_login()` verifies credentials and `active` flag before granting a session |
| Per-command check | Every server handler calls `auth_check_role()` before executing |
| First-login policy | Bidders with `need_pw_change=1` are forced through a password-change screen |
| Admin override | Admins can close any item; auctioneers can only close their own (`auctioneer_id` check) |

---

### 4.2 File Locking

All three data files (`users.dat`, `items.dat`, `bids.dat`) use **POSIX `fcntl()` advisory locking**. Shared (read) locks allow concurrent readers; exclusive (write) locks serialise writes. `F_SETLKW` is used so a thread **blocks** rather than fails when a conflicting lock is held.

```c
static void lock_file(int fd, int type) {
    struct flock fl = {0};
    fl.l_type   = type;       /* F_RDLCK or F_WRLCK */
    fl.l_whence = SEEK_SET;
    fl.l_len    = 0;          /* whole file          */
    fcntl(fd, F_SETLKW, &fl); /* blocks until granted */
}
```

| Operation | Lock type | Function |
|---|---|---|
| Read (`_load`) | `F_RDLCK` | `userdb_load()`, `itemdb_load()` |
| Write (`_save`) | `F_WRLCK` | `userdb_save()`, `itemdb_save()`, `bid_save()` |
| Unlock | `F_UNLCK` | Called after `fflush()`, before `fclose()` |

---

### 4.3 Concurrency Control

The server uses a **thread-per-client model**: each accepted connection gets a detached POSIX thread. `bid_engine.c` uses **two synchronisation primitives** to protect the critical section.

```c
/* 1. Semaphore — cap simultaneous bidders to SEM_LIMIT=10 */
if (sem_trywait(g_sem) != 0)
    return -3;  /* server busy */

/* 2. Mutex — serialise the price-check + update */
pthread_mutex_lock(&g_bid_mutex);
    /* ... validate bid amount, record bid, update price ... */
pthread_mutex_unlock(&g_bid_mutex);
sem_post(g_sem);
```

| Primitive | Purpose |
|---|---|
| `pthread_mutex` | Guards in-memory bid array and `current_price` update |
| POSIX semaphore (`/auction_bid_sem`) | Limits simultaneous active bidders to 10 |
| `PTHREAD_CREATE_DETACHED` | Avoids `pthread_join()` overhead for each client thread |

---

### 4.4 Data Consistency

Data consistency is maintained at **two levels**: in-memory (mutex-protected atomic price check + update) and on-disk (fcntl write-lock spanning the entire save operation).

| Issue prevented | Mechanism |
|---|---|
| **Race condition** | Mutex held across minimum-price check AND `current_price` update — two threads can never both pass the check with the same price |
| **Dirty read** | `F_RDLCK` on disk prevents a reading thread from seeing a partially written file |
| **Lost update** | `F_WRLCK` on disk prevents two threads truncating and writing the file concurrently |
| **Auction expiry** | `end_time` check and `ITEM_EXPIRED` status update happen inside the same mutex block |

---

### 4.5 Socket Programming

Communication uses a **custom binary protocol over TCP/IP**. A fixed-size `Message` struct is sent with `send()` and received with `recv()`, eliminating framing complexity.

```c
typedef struct {
    MsgType type;           /* MSG_LOGIN, MSG_PLACE_BID, etc.  */
    int     sender_id;
    char    payload[2048];  /* key=value pairs                 */
    int     status;         /* 0 = success, -1 = error         */
} Message;
```

**Server setup:**
```c
socket() → setsockopt(SO_REUSEADDR) → bind(:9090) → listen(BACKLOG=10) → accept() loop
```

**Client connect:**
```c
socket() → inet_pton() → connect()   /* fresh socket per login session */
```

| Detail | Value |
|---|---|
| Protocol | TCP (`SOCK_STREAM`) on port **9090** |
| Message types | 19 types: `MSG_LOGIN`, `MSG_PLACE_BID`, `MSG_ADD_ITEM`, `MSG_START_AUCTION`, `MSG_MY_BIDS`, … |
| Thread dispatch | `accept()` → `calloc(ClientCtx)` → `pthread_create(client_thread)` |

---

### 4.6 Inter-Process Communication (IPC)

Two IPC mechanisms are implemented: **POSIX shared memory** for a live auction scoreboard and **POSIX signals** for graceful server shutdown.

#### Shared Memory — Live Scoreboard

`ipc_shm_init()` creates a shared-memory segment large enough to hold a `SharedBoard` struct containing per-item live prices, bid counts, and highest bidder IDs. Every successful bid updates it via `ipc_shm_update()` — no network round-trip needed.

```c
/* ipc.c — create and attach */
key_t key  = ftok(".", 'A');
g_shm_id   = shmget(key, sizeof(SharedBoard), IPC_CREAT | 0666);
g_board    = (SharedBoard *)shmat(g_shm_id, NULL, 0);
```

| Step | Call |
|---|---|
| Create segment | `shmget(ftok('.','A'), sizeof(SharedBoard), IPC_CREAT\|0666)` |
| Attach | `shmat(g_shm_id, NULL, 0)` |
| Update on bid | `ipc_shm_update(item_id, price, bid_count, top_bidder)` in `bid_place()` |
| Cleanup | `shmdt()` then `shmctl(IPC_RMID)` on shutdown |

#### Signals — Graceful Shutdown

`ipc_signal_setup()` registers a `sigaction` handler for `SIGINT`, `SIGTERM`, and `SIGUSR1`.

```c
static volatile sig_atomic_t g_running = 1;

static void sig_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM)
        g_running = 0;        /* triggers graceful shutdown */
    else if (sig == SIGUSR1)
        /* force-refresh live board */;
}
```

---

## 📁 Project Structure

```
online-auction-system/
├── common.h            — Shared structs, enums, constants
├── auth.h / auth.c     — Login & role-check logic
├── user_db.h / .c      — User persistence (fcntl locks)
├── item_db.h / .c      — Item persistence (fcntl locks)
├── bid_engine.h / .c   — Bid logic (mutex + semaphore)
├── ipc.h / ipc.c       — Shared memory + signal handling
├── logger.h / .c       — Timestamped log to file + stderr
├── server.h / .c       — TCP server, per-client threads
├── main_server.c       — Server entry point
├── main_client.c       — Interactive CLI client
├── data/               — Binary data files (auto-created)
├── logs/               — Audit log (auto-created)
└── Makefile
```

---

## 🚀 Installation & Setup

### Requirements

- Linux / Unix operating system (Ubuntu 20.04+ recommended)
- GCC compiler (`gcc 9+`)
- POSIX threads library (`pthread`)
- POSIX real-time library (`librt`) for semaphores

### Build

```bash
# Build both binaries
make

# Other targets
make clean       # remove all build artefacts
make run_server  # start the server
make run_client  # start the client (separate terminal)
```

### Manual Compile

```bash
# Server
gcc -Wall -g logger.c user_db.c item_db.c bid_engine.c ipc.c auth.c server.c main_server.c \
    -o auction_server -lpthread -lrt

# Client
gcc -Wall -g main_client.c -o auction_client -lpthread -lrt
```

### Default Accounts (First Run)

On first run the server seeds three default accounts:

| Username | Password | Role | Note |
|---|---|---|---|
| `admin` | `admin123` | Administrator | Change after first login |
| `seller1` | `seller123` | Auctioneer | Default auctioneer |
| `bidder1` | `bidder123` | Bidder | Forced password change on login |

---

## 🖥 Usage

### Start the Server
```bash
./auction_server
```

### Start the Client (separate terminal)
```bash
./auction_client           # connects to localhost by default
./auction_client 192.168.1.5   # connect to a specific IP
```

### Client Flow

```
1. Login screen  →  enter username / password
2. Role dashboard  →  routed automatically by role
3. Select an option from the menu
4. Follow the prompts
5. Enter 0 to logout
```

### Stopping the Server
```
Ctrl+C   →   SIGINT triggers graceful shutdown + shared memory cleanup
```

---

## 🧩 Challenges & Solutions

| Challenge | Solution |
|---|---|
| **Race condition on bid price** | Moved the minimum-price check AND the price-update into a single mutex-held block in `bid_place()`. Two threads can no longer both pass the check with the same `current_price`. |
| **`fcntl` lock order with `FILE*`** | Lock must be acquired on the raw `fd` before `fdopen()`, and released after `fflush()` but before `fclose()`. Mixing lock and `FILE*` close order caused subtle unlock-after-close bugs. |
| **Invalid menu input triggers silent logout** | `atoi("abc")` returns `0` which equals the Logout option. Fixed `get_choice()` to reject any input whose first character is not a digit, so strings re-prompt instead of logging out. |
| **Stale semaphore from previous run** | `sem_unlink(SEM_NAME)` is called before `sem_open()` in `bid_load()` to destroy any semaphore left over from a previous server crash. |
| **Shared memory key collision** | `ftok('.','A')` can collide with other processes sharing the same directory. Documented as a known limitation; `SHM_KEY 0xAC10` is available as a fallback constant. |
| **Thread safety in item expiry** | `itemdb_get_active()` was mutating the global item array without a lock. Expiry mutation was moved inside the `bid_place()` mutex section for auction-end checks. |

---

## 🔮 Limitations & Future Work

### Current Limitations

- **Plaintext passwords** — stored as plain strings; hashing (SHA-256 / bcrypt) should be applied
- **No network encryption** — all TCP traffic is unencrypted; TLS/SSL needed for production
- **Fixed-size arrays** — `MAX_USERS=50`, `MAX_ITEMS=100`, `MAX_BIDS=500` are compile-time constants
- **No real-time countdown on client** — time-remaining is computed at listing time only
- **Single-server only** — no replication or failover mechanism

### Potential Enhancements

- Password hashing using `libsodium` or OpenSSL SHA-256
- TLS wrapping of the TCP socket with OpenSSL
- Replace flat binary files with SQLite for arbitrary-scale persistence
- `epoll`-based event-driven server for better scalability
- Web-based front-end over a REST API layer
- Real-time push notifications for bid updates

---

## 📊 OS Concept Coverage Summary

| Guideline | Concept | Files / Functions |
|---|---|---|
| **4.1** | Role-Based Authorization | `auth.c` → `auth_check_role()`, every handler in `server.c` |
| **4.2** | File Locking (`fcntl`) | `user_db.c`, `item_db.c`, `bid_engine.c` — `F_RDLCK` / `F_WRLCK` |
| **4.3** | Concurrency Control | `bid_engine.c` — `pthread_mutex` + POSIX semaphore; `server.c` — thread per client |
| **4.4** | Data Consistency | `bid_engine.c` — atomic price-check + update under mutex; `fcntl` write-lock on save |
| **4.5** | Socket Programming | `server.c` — TCP server; `main_client.c` — TCP client; 19 message types |
| **4.6** | IPC — Shared Memory | `ipc.c` — `shmget` / `shmat` `SharedBoard`; updated on every bid |
| **4.6** | IPC — Signals | `ipc.c` — `sigaction(SIGINT / SIGTERM / SIGUSR1)`; graceful shutdown |

---

<div align="center">

*Developed as part of EGC 301P – Operating Systems Laboratory*

</div>
