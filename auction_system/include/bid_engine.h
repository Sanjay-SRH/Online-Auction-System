#ifndef BID_ENGINE_H
#define BID_ENGINE_H

#include "common.h"

/* Place a bid; returns 0 on success, negative on error. */
int  bid_place(int item_id, int bidder_id, double amount);

/* Retrieve all bids for an item. Caller frees returned array. */
Bid *bid_get_for_item(int item_id, int *count);

/* Persist bids to file. */
int  bid_save(void);
int  bid_load(void);

#endif /* BID_ENGINE_H */
