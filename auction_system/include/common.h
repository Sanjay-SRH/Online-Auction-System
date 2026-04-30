#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

/* ── Sizes ── */
#define MAX_USERNAME      32
#define MAX_PASSWORD      64
#define MAX_ITEM_NAME     128
#define MAX_DESCRIPTION   256
#define MAX_USERS         50
#define MAX_ITEMS         100
#define MAX_BIDS          500
#define LOG_FILE          "logs/auction.log"
#define USERS_FILE        "data/users.dat"
#define ITEMS_FILE        "data/items.dat"
#define BIDS_FILE         "data/bids.dat"
/* ── Shared memory key ── */
#define SHM_KEY           0xAC10

/* ── Roles ── */
typedef enum {
    ROLE_ADMIN      = 0,
    ROLE_AUCTIONEER = 1,
    ROLE_BIDDER     = 2,
    ROLE_GUEST      = 3
} Role;

/* ── Item status ── */
typedef enum {
    ITEM_PENDING  = 0,
    ITEM_ACTIVE   = 1,
    ITEM_SOLD     = 2,
    ITEM_EXPIRED  = 3
} ItemStatus;

/* ── User record ── */
typedef struct {
    int    id;
    char   username[MAX_USERNAME];
    char   password[MAX_PASSWORD];   /* stored as plain text for simplicity */
    Role   role;
    int    active;
    double balance;
    int    need_pw_change;           /* 1 = must change password on next login */
} User;

/* ── Auction item record ── */
typedef struct {
    int        id;
    char       name[MAX_ITEM_NAME];
    char       description[MAX_DESCRIPTION];
    double     starting_price;
    double     current_price;
    double     min_increment;
    int        auctioneer_id;
    int        highest_bidder_id;
    time_t     end_time;           /* Unix timestamp */
    ItemStatus status;
    int        active;
} Item;

/* ── Bid record ── */
typedef struct {
    int    id;
    int    item_id;
    int    bidder_id;
    double amount;
    time_t timestamp;
} Bid;

/* ── Network message (client ↔ server) ── */
typedef enum {
    MSG_LOGIN,
    MSG_LOGOUT,
    MSG_LIST_ITEMS,
    MSG_PLACE_BID,
    MSG_ADD_ITEM,
    MSG_CLOSE_ITEM,
    MSG_VIEW_BIDS,
    MSG_ADD_USER,
    MSG_DISABLE_USER,
    MSG_CHANGE_PASSWORD,
    MSG_LIST_USERS,
    MSG_RESPONSE,
    MSG_NOTIFICATION,
    MSG_PING,
    MSG_START_AUCTION,
    MSG_LIST_ALL_ITEMS,
    MSG_MY_BIDS,
    MSG_REMOVE_ITEM,
    MSG_UPDATE_ITEM_PRICE
} MsgType;

typedef struct {
    MsgType type;
    int     sender_id;
    char    payload[2048];   /* JSON-like key=value pairs */
    int     status;          /* 0 = success, -1 = error */
} Message;

/* ── Shared memory live scores ── */
typedef struct {
    int    item_id;
    double current_price;
    int    bid_count;
    int    highest_bidder_id;
} LiveItem;

typedef struct {
    int      count;
    LiveItem items[MAX_ITEMS];
} SharedBoard;

/* ── Utility ── */
static inline const char *role_str(Role r) {
    switch (r) {
        case ROLE_ADMIN:      return "Admin";
        case ROLE_AUCTIONEER: return "Auctioneer";
        case ROLE_BIDDER:     return "Bidder";
        default:              return "Guest";
    }
}

#endif /* COMMON_H */