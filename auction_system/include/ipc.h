#ifndef IPC_H
#define IPC_H

#include "common.h"

/* ── Shared-memory live board (IPC – Shared Memory) ── */
int          ipc_shm_init(void);
SharedBoard *ipc_shm_get(void);
void         ipc_shm_update(int item_id, double price, int bid_count, int top_bidder);
void         ipc_shm_destroy(void);

/* ── Signal helpers ── */
void ipc_signal_setup(void);

#endif /* IPC_H */