/*
 * server.c
 * Multi-threaded auction server.
 *
 * Satisfies Socket Programming (4.5):
 *   - TCP socket, client-server model
 *   - One pthread per connected client (Thread per client)
 *
 * Satisfies Concurrency Control (4.3):
 *   - pthread per client; bid_engine serialises via mutex + semaphore
 *
 * Satisfies Role-Based Authorization (4.1):
 *   - Every command checks auth_check_role() before executing
 */
#include "server.h"
#include "common.h"
#include "auth.h"
#include "user_db.h"
#include "item_db.h"
#include "bid_engine.h"
#include "ipc.h"
#include "logger.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

/* ── Per-client state ── */
typedef struct {
    int  sock;
    int  user_id;   /* -1 = not logged in */
} ClientCtx;

/* ── Helpers ── */
static void send_response(int sock, int status, const char *msg) {
    Message resp = {0};
    resp.type   = MSG_RESPONSE;
    resp.status = status;
    strncpy(resp.payload, msg, sizeof(resp.payload) - 1);
    send(sock, &resp, sizeof(resp), 0);
}

/* ── Command handlers ── */
static void handle_login(ClientCtx *ctx, const Message *msg) {
    char uname[MAX_USERNAME] = {0};
    char pass[MAX_PASSWORD]  = {0};
    sscanf(msg->payload, "username=%31s password=%63s", uname, pass);

    User *u = auth_login(uname, pass);
    if (!u) {
        send_response(ctx->sock, -1, "Login failed: invalid username or password");
        return;
    }
    ctx->user_id = u->id;

    /* Encode role + need_pw_change into payload so client can route to the right dashboard */
    char buf[256];
    snprintf(buf, sizeof(buf),
             "role=%d need_pw_change=%d username=%s user_id=%d greeting=Welcome %s! Role: %s",
             (int)u->role, u->need_pw_change,
             u->username, u->id, u->username, role_str(u->role));
    send_response(ctx->sock, 0, buf);
}

static void handle_list_items(ClientCtx *ctx) {
    int count = 0;
    Item *items = itemdb_get_active(&count);
    char buf[2048] = {0};
    if (count == 0) {
        snprintf(buf, sizeof(buf), "No active auctions right now.");
    } else {
        for (int i = 0; i < count && strlen(buf) < 1900; i++) {
            /* Convert end_time to a human-readable string */
            char time_str[32] = "N/A";
            if (items[i].end_time > 0) {
                time_t remaining = items[i].end_time - time(NULL);
                if (remaining > 0)
                    snprintf(time_str, sizeof(time_str), "%ldm %lds",
                             remaining / 60, remaining % 60);
                else
                    snprintf(time_str, sizeof(time_str), "Ending soon");
            }
            char line[600];
            snprintf(line, sizeof(line),
                     "[%d] %s\n"
                     "    Desc        : %s\n"
                     "    Start Price : $%.2f\n"
                     "    Current     : $%.2f\n"
                     "    Min Raise   : $%.2f\n"
                     "    Time Left   : %s\n\n",
                     items[i].id, items[i].name,
                     items[i].description[0] ? items[i].description : "(no description)",
                     items[i].starting_price,
                     items[i].current_price,
                     items[i].min_increment,
                     time_str);
            strncat(buf, line, sizeof(buf) - strlen(buf) - 1);
        }
    }
    send_response(ctx->sock, 0, buf);
}

/* List ALL items (all statuses) — for all dashboards */
static void handle_list_all_items(ClientCtx *ctx) {
    int   count = 0;
    Item *items = itemdb_get_all(&count);
    char  buf[2048] = {0};
    if (count == 0) {
        snprintf(buf, sizeof(buf), "No items in the system yet.");
    } else {
        for (int i = 0; i < count && strlen(buf) < 1900; i++) {
            const char *status_str;
            switch (items[i].status) {
                case ITEM_PENDING:  status_str = "PENDING (not started)"; break;
                case ITEM_ACTIVE:   status_str = "ACTIVE (bidding open)"; break;
                case ITEM_SOLD:     status_str = "SOLD";                  break;
                case ITEM_EXPIRED:  status_str = "EXPIRED (unsold)";      break;
                default:            status_str = "UNKNOWN";               break;
            }
            /* For sold items also show winner */
            char winner_info[64] = "";
            if (items[i].status == ITEM_SOLD && items[i].highest_bidder_id > 0) {
                snprintf(winner_info, sizeof(winner_info),
                         " | Winner: User #%d @ $%.2f",
                         items[i].highest_bidder_id, items[i].current_price);
            } else if (items[i].status == ITEM_SOLD && items[i].highest_bidder_id <= 0) {
                snprintf(winner_info, sizeof(winner_info), " | Unsold");
            }
            char line[256];
            snprintf(line, sizeof(line),
                     "[%d] %-24s  Status: %s%s\n"
                     "     Price: $%.2f  |  Start: $%.2f\n\n",
                     items[i].id, items[i].name,
                     status_str, winner_info,
                     items[i].current_price,
                     items[i].starting_price);
            strncat(buf, line, sizeof(buf) - strlen(buf) - 1);
        }
    }
    send_response(ctx->sock, 0, buf);
}

static void handle_place_bid(ClientCtx *ctx, const Message *msg) {
    if (ctx->user_id < 0) {
        send_response(ctx->sock, -1, "Not logged in");
        return;
    }
    User *u = userdb_find_by_id(ctx->user_id);
    if (!auth_check_role(u, ROLE_BIDDER)) {
        send_response(ctx->sock, -1, "Permission denied");
        return;
    }
    int    item_id = 0;
    double amount  = 0.0;
    sscanf(msg->payload, "item_id=%d amount=%lf", &item_id, &amount);

    int rc = bid_place(item_id, ctx->user_id, amount);
    char resp[128];
    if (rc == 0)
        snprintf(resp, sizeof(resp), "Bid of $%.2f placed on item %d!", amount, item_id);
    else if (rc == -3)
        snprintf(resp, sizeof(resp), "Server busy, try again shortly.");
    else if (rc == -5)
        snprintf(resp, sizeof(resp), "Bid too low. Please bid higher than the current price plus minimum increment.");
    else if (rc == -4)
        snprintf(resp, sizeof(resp), "Auction has ended.");
    else
        snprintf(resp, sizeof(resp), "Bid failed (code %d).", rc);

    send_response(ctx->sock, rc, resp);
}

static void handle_add_item(ClientCtx *ctx, const Message *msg) {
    if (ctx->user_id < 0) { send_response(ctx->sock, -1, "Not logged in"); return; }
    User *u = userdb_find_by_id(ctx->user_id);
    if (!auth_check_role(u, ROLE_AUCTIONEER)) {
        send_response(ctx->sock, -1, "Only Auctioneers or Admins can add items");
        return;
    }
    Item item = {0};
    item.status = ITEM_PENDING;
    item.active = 0;

    /* ── Parse name (may contain spaces) ── */
    char *p;
    p = strstr(msg->payload, "name=");
    if (p) {
        p += 5;
        int i = 0;
        /* name ends where " price=" begins */
        while (*p && !(p[0]==' ' && strncmp(p," price=",7)==0) && i < MAX_ITEM_NAME - 1)
            item.name[i++] = *p++;
        item.name[i] = '\0';
    }

    /* ── Parse numeric fields ── */
    p = strstr(msg->payload, "price=");
    if (p) sscanf(p, "price=%lf", &item.starting_price);

    p = strstr(msg->payload, "increment=");
    if (p) sscanf(p, "increment=%lf", &item.min_increment);

    /* ── Parse description (may contain spaces, comes last) ── */
    p = strstr(msg->payload, "desc=");
    if (p) {
        p += 5;
        strncpy(item.description, p, MAX_DESCRIPTION - 1);
    }

    /* current_price starts at starting_price */
    item.current_price = item.starting_price;
    item.auctioneer_id = ctx->user_id;
    item.end_time      = 0;   /* not started yet */

    if (itemdb_add(&item) == 0) {
        char resp[256];
        snprintf(resp, sizeof(resp), "Item '%s' added (id=%d) — use 'Start Auction' to begin bidding.", item.name, item.id);
        ipc_shm_update(item.id, item.starting_price, 0, -1);
        send_response(ctx->sock, 0, resp);
    } else {
        send_response(ctx->sock, -1, "Failed to add item");
    }
}

static void handle_close_item(ClientCtx *ctx, const Message *msg) {
    if (ctx->user_id < 0) { send_response(ctx->sock, -1, "Not logged in"); return; }
    User *u = userdb_find_by_id(ctx->user_id);
    if (!auth_check_role(u, ROLE_AUCTIONEER)) {
        send_response(ctx->sock, -1, "Permission denied");
        return;
    }
    int item_id = 0;
    sscanf(msg->payload, "item_id=%d", &item_id);
    Item *item = itemdb_find(item_id);
    if (!item) { send_response(ctx->sock, -1, "Item not found"); return; }
    /* Auctioneers can only close their own items; admins can close any */
    if (u->role == ROLE_AUCTIONEER && item->auctioneer_id != ctx->user_id) {
        send_response(ctx->sock, -1, "Not your item");
        return;
    }
    /* Snapshot before closing */
    int    winner_id    = item->highest_bidder_id;
    double winning_bid  = item->current_price;
    char   item_name[MAX_ITEM_NAME];
    strncpy(item_name, item->name, sizeof(item_name) - 1);

    itemdb_close(item_id);

    char resp[256];
    if (winner_id > 0) {
        snprintf(resp, sizeof(resp),
                 "Item %d ('%s') closed.\n  Winner: User #%d  |  Winning Bid: $%.2f",
                 item_id, item_name, winner_id, winning_bid);
    } else {
        snprintf(resp, sizeof(resp),
                 "Item %d ('%s') closed.\n  Result: UNSOLD (no bids were placed).",
                 item_id, item_name);
    }
    send_response(ctx->sock, 0, resp);
}

static void handle_view_bids(ClientCtx *ctx, const Message *msg) {
    int item_id = 0;
    sscanf(msg->payload, "item_id=%d", &item_id);

    /* itemdb_find() only returns active items; use get_all so closed/sold items work too */
    int   all_count = 0;
    Item *all_items = itemdb_get_all(&all_count);
    Item *item = NULL;
    for (int i = 0; i < all_count; i++) {
        if (all_items[i].id == item_id) { item = &all_items[i]; break; }
    }

    int   count = 0;
    Bid  *bids  = bid_get_for_item(item_id, &count);
    char  buf[512] = {0};
    snprintf(buf, sizeof(buf), "Bids for item %d (%d total):\n", item_id, count);
    for (int i = 0; i < count && strlen(buf) < 460; i++) {
        char line[80];
        snprintf(line, sizeof(line),
                 "  Bid #%d : bidder_id = %d  $%.2f\n",
                 bids[i].id, bids[i].bidder_id, bids[i].amount);
        strncat(buf, line, sizeof(buf) - strlen(buf) - 1);
    }

    /* Show winner */
    if (item && item->highest_bidder_id > 0) {
        char winner[80];
        if (item->status == ITEM_ACTIVE) {
            snprintf(winner, sizeof(winner),
                    " >> Current highest bidder: bidder_id=%d at $%.2f\n",
                    item->highest_bidder_id, item->current_price);
        } else {
            snprintf(winner, sizeof(winner),
                    " >> WINNER: bidder_id=%d with winning bid $%.2f\n",
                    item->highest_bidder_id, item->current_price);
        }
        strncat(buf, winner, sizeof(buf) - strlen(buf) - 1);
    } else if (count == 0) {
        strncat(buf, " No bids placed yet.\n", sizeof(buf) - strlen(buf) - 1);
    }
    send_response(ctx->sock, 0, buf);
}

static void handle_add_user(ClientCtx *ctx, const Message *msg) {
    if (ctx->user_id < 0) { send_response(ctx->sock, -1, "Not logged in"); return; }
    User *caller = userdb_find_by_id(ctx->user_id);
    if (!auth_check_role(caller, ROLE_ADMIN)) {
        send_response(ctx->sock, -1, "Only Admins can add users");
        return;
    }
    User nu = {0};
    nu.active = 1;
    int role_int = ROLE_BIDDER;
    sscanf(msg->payload, "username=%31s password=%63s role=%d",
           nu.username, nu.password, &role_int);
    nu.role = (Role)role_int;
    /* Bidders must change their password on first login */
    nu.need_pw_change = (nu.role == ROLE_BIDDER) ? 1 : 0;
    int rc = userdb_add(&nu);
    if (rc == 0)
        send_response(ctx->sock, 0, "User added");
    else if (rc == -2)
        send_response(ctx->sock, -1, "Username already exists");
    else
        send_response(ctx->sock, -1, "Failed to add user");
}

static void handle_disable_user(ClientCtx *ctx, const Message *msg) {
    if (ctx->user_id < 0) { send_response(ctx->sock, -1, "Not logged in"); return; }
    User *caller = userdb_find_by_id(ctx->user_id);
    if (!auth_check_role(caller, ROLE_ADMIN)) {
        send_response(ctx->sock, -1, "Only Admins can disable users");
        return;
    }
    int uid = 0;
    sscanf(msg->payload, "user_id=%d", &uid);
    userdb_disable(uid);
    send_response(ctx->sock, 0, "User disabled");
}

static void handle_change_password(ClientCtx *ctx, const Message *msg) {
    if (ctx->user_id < 0) { send_response(ctx->sock, -1, "Not logged in"); return; }
    char new_pass[MAX_PASSWORD] = {0};
    sscanf(msg->payload, "new_password=%63s", new_pass);
    if (strlen(new_pass) < 4) {
        send_response(ctx->sock, -1, "Password too short (min 4 chars)");
        return;
    }
    if (userdb_change_password(ctx->user_id, new_pass) == 0) {
        send_response(ctx->sock, 0, "Password changed successfully!");
    } else {
        send_response(ctx->sock, -1, "Failed to change password");
    }
}

static void handle_list_users(ClientCtx *ctx) {
    if (ctx->user_id < 0) { send_response(ctx->sock, -1, "Not logged in"); return; }
    User *caller = userdb_find_by_id(ctx->user_id);
    if (!auth_check_role(caller, ROLE_ADMIN)) {
        send_response(ctx->sock, -1, "Permission denied");
        return;
    }
    int   count = 0;
    User *users = userdb_get_all(&count);
    char  buf[900] = {0};
    for (int i = 0; i < count && strlen(buf) < 860; i++) {
        char line[100];
        snprintf(line, sizeof(line),
                 "[%d] %-16s %-12s %s\n",
                 users[i].id, users[i].username,
                 role_str(users[i].role),
                 users[i].active ? "active" : "disabled");
        strncat(buf, line, sizeof(buf) - strlen(buf) - 1);
    }
    send_response(ctx->sock, 0, buf[0] ? buf : "No users found.");
}

static void handle_start_auction(ClientCtx *ctx, const Message *msg) {
    if (ctx->user_id < 0) { send_response(ctx->sock, -1, "Not logged in"); return; }
    User *u = userdb_find_by_id(ctx->user_id);
    if (!auth_check_role(u, ROLE_AUCTIONEER)) {
        send_response(ctx->sock, -1, "Permission denied");
        return;
    }
    int item_id = 0;
    double duration_sec = 300;
    sscanf(msg->payload, "item_id=%d duration=%lf", &item_id, &duration_sec);

    Item *item = itemdb_find(item_id);
    if (!item) { send_response(ctx->sock, -1, "Item not found"); return; }
    if (u->role == ROLE_AUCTIONEER && item->auctioneer_id != ctx->user_id) {
        send_response(ctx->sock, -1, "Not your item"); return;
    }
    if (item->status == ITEM_ACTIVE) {
        send_response(ctx->sock, -1, "Auction already running"); return;
    }
    item->status        = ITEM_ACTIVE;
    item->active        = 1;
    item->current_price = item->starting_price;
    item->end_time      = time(NULL) + (time_t)duration_sec;
    itemdb_update(item);

    char resp[300];
    snprintf(resp, sizeof(resp),
             "Auction started for item %d '%s'! Ends in %.0f seconds.", item_id, item->name, duration_sec);
    send_response(ctx->sock, 0, resp);
}

/* Show all bids placed by the calling bidder, with won/lost/ongoing status */
static void handle_my_bids(ClientCtx *ctx) {
    if (ctx->user_id < 0) { send_response(ctx->sock, -1, "Not logged in"); return; }

    int   all_item_count = 0;
    Item *all_items = itemdb_get_all(&all_item_count);

    char buf[2048] = {0};
    int  item_count = 0;   /* number of distinct items the bidder bid on */

    for (int i = 0; i < all_item_count; i++) {
        int   bc = 0;
        Bid  *bids = bid_get_for_item(all_items[i].id, &bc);

        /* Find this bidder's highest bid on this item */
        double my_highest = -1.0;
        int    bid_times  = 0;
        for (int j = 0; j < bc; j++) {
            if (bids[j].bidder_id != ctx->user_id) continue;
            bid_times++;
            if (bids[j].amount > my_highest)
                my_highest = bids[j].amount;
        }
        if (bid_times == 0) continue;   /* bidder never bid on this item */

        item_count++;

        const char *outcome;
        if (all_items[i].status == ITEM_ACTIVE) {
            if (all_items[i].highest_bidder_id == ctx->user_id)
                outcome = "ONGOING  (you are currently HIGHEST bidder)";
            else
                outcome = "ONGOING  (you have been outbid)";
        } else if (all_items[i].status == ITEM_SOLD) {
            if (all_items[i].highest_bidder_id == ctx->user_id)
                outcome = "WON";
            else
                outcome = "LOST";
        } else {
            outcome = "AUCTION ENDED (unsold/expired)";
        }

        char line[300];
        snprintf(line, sizeof(line),
                 "  Item #%d : %-20s\n"
                 "    Your Highest Bid : $%.2f  |  Total Bids by You : %d\n"
                 "    Status           : %s\n\n",
                 all_items[i].id, all_items[i].name,
                 my_highest, bid_times,
                 outcome);
        strncat(buf, line, sizeof(buf) - strlen(buf) - 1);
    }

    if (item_count == 0) {
        snprintf(buf, sizeof(buf), "You have not placed any bids yet.");
    } else {
        char header[80];
        snprintf(header, sizeof(header), "Your bids on %d item(s):\n\n", item_count);
        char tmp[2200];
        snprintf(tmp, sizeof(tmp), "%s%s", header, buf);
        strncpy(buf, tmp, sizeof(buf) - 1);
    }
    send_response(ctx->sock, 0, buf);
}

/* Auctioneer: remove a PENDING item (cannot remove active/sold) */
static void handle_remove_item(ClientCtx *ctx, const Message *msg) {
    if (ctx->user_id < 0) { send_response(ctx->sock, -1, "Not logged in"); return; }
    User *u = userdb_find_by_id(ctx->user_id);
    if (!auth_check_role(u, ROLE_AUCTIONEER)) {
        send_response(ctx->sock, -1, "Permission denied"); return;
    }
    int item_id = 0;
    sscanf(msg->payload, "item_id=%d", &item_id);

    /* Look up in all items (including pending which have active=0) */
    int   all_count = 0;
    Item *all_items = itemdb_get_all(&all_count);
    Item *item = NULL;
    for (int i = 0; i < all_count; i++)
        if (all_items[i].id == item_id) { item = &all_items[i]; break; }

    if (!item) { send_response(ctx->sock, -1, "Item not found"); return; }
    if (u->role == ROLE_AUCTIONEER && item->auctioneer_id != ctx->user_id) {
        send_response(ctx->sock, -1, "Not your item"); return;
    }
    if (item->status == ITEM_ACTIVE) {
        send_response(ctx->sock, -1,
            "Cannot remove an active auction. Close it first."); return;
    }
    if (item->status == ITEM_SOLD) {
        send_response(ctx->sock, -1, "Cannot remove a sold item."); return;
    }
    /* Mark as inactive/expired to effectively delete */
    item->active = 0;
    item->status = ITEM_EXPIRED;
    itemdb_update(item);

    char resp[200];
    snprintf(resp, sizeof(resp), "Item #%d '%s' removed successfully.", item_id, item->name);
    send_response(ctx->sock, 0, resp);
}

/* Auctioneer: update starting price of a PENDING item */
static void handle_update_item_price(ClientCtx *ctx, const Message *msg) {
    if (ctx->user_id < 0) { send_response(ctx->sock, -1, "Not logged in"); return; }
    User *u = userdb_find_by_id(ctx->user_id);
    if (!auth_check_role(u, ROLE_AUCTIONEER)) {
        send_response(ctx->sock, -1, "Permission denied"); return;
    }
    int    item_id = 0;
    double new_price = 0.0, new_increment = 0.0;
    sscanf(msg->payload, "item_id=%d price=%lf increment=%lf",
           &item_id, &new_price, &new_increment);

    int   all_count = 0;
    Item *all_items = itemdb_get_all(&all_count);
    Item *item = NULL;
    for (int i = 0; i < all_count; i++)
        if (all_items[i].id == item_id) { item = &all_items[i]; break; }

    if (!item) { send_response(ctx->sock, -1, "Item not found"); return; }
    if (u->role == ROLE_AUCTIONEER && item->auctioneer_id != ctx->user_id) {
        send_response(ctx->sock, -1, "Not your item"); return;
    }
    if (item->status != ITEM_PENDING) {
        send_response(ctx->sock, -1,
            "Can only update price for PENDING items (not yet started)."); return;
    }
    if (new_price > 0) {
        item->starting_price = new_price;
        item->current_price  = new_price;
    }
    if (new_increment > 0)
        item->min_increment = new_increment;

    itemdb_update(item);
    char resp[200];
    snprintf(resp, sizeof(resp),
             "Item #%d '%s' updated. New start price: $%.2f  Min increment: $%.2f",
             item_id, item->name, item->starting_price, item->min_increment);
    send_response(ctx->sock, 0, resp);
}

/* ── Per-client thread ── */
static void *client_thread(void *arg) {
    ClientCtx *ctx = (ClientCtx *)arg;
    LOG_INFO("Client thread started (sock=%d)", ctx->sock);

    Message msg;
    while (1) {
        ssize_t n = recv(ctx->sock, &msg, sizeof(msg), 0);
        if (n <= 0) break;

        switch (msg.type) {
            case MSG_LOGIN:           handle_login(ctx, &msg);           break;
            case MSG_LIST_ITEMS:      handle_list_items(ctx);            break;
            case MSG_PLACE_BID:       handle_place_bid(ctx, &msg);       break;
            case MSG_ADD_ITEM:        handle_add_item(ctx, &msg);        break;
            case MSG_CLOSE_ITEM:      handle_close_item(ctx, &msg);      break;
            case MSG_VIEW_BIDS:       handle_view_bids(ctx, &msg);       break;
            case MSG_ADD_USER:        handle_add_user(ctx, &msg);        break;
            case MSG_DISABLE_USER:    handle_disable_user(ctx, &msg);    break;
            case MSG_CHANGE_PASSWORD: handle_change_password(ctx, &msg); break;
            case MSG_LIST_USERS:      handle_list_users(ctx);            break;
            case MSG_START_AUCTION:   handle_start_auction(ctx, &msg);   break;
            case MSG_LIST_ALL_ITEMS:  handle_list_all_items(ctx);         break;
            case MSG_MY_BIDS:         handle_my_bids(ctx);                break;
            case MSG_REMOVE_ITEM:     handle_remove_item(ctx, &msg);      break;
            case MSG_UPDATE_ITEM_PRICE: handle_update_item_price(ctx, &msg); break;
            case MSG_LOGOUT:
                send_response(ctx->sock, 0, "Goodbye!");
                goto done;
            case MSG_PING:
                send_response(ctx->sock, 0, "PONG");
                break;
            default:
                send_response(ctx->sock, -1, "Unknown command");
        }
    }
done:
    LOG_INFO("Client disconnected (sock=%d)", ctx->sock);
    close(ctx->sock);
    free(ctx);
    return NULL;
}

/* ── Main server loop ── */
void server_run(void) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); exit(1); }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(SERVER_PORT);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); exit(1);
    }
    if (listen(server_fd, BACKLOG) < 0) {
        perror("listen"); exit(1);
    }

    LOG_INFO("Auction server listening on port %d", SERVER_PORT);

    while (1) {
        struct sockaddr_in cli_addr;
        socklen_t cli_len = sizeof(cli_addr);
        int cli_sock = accept(server_fd, (struct sockaddr *)&cli_addr, &cli_len);
        if (cli_sock < 0) {
            LOG_WARN("accept failed: %s", strerror(errno));
            continue;
        }
        LOG_INFO("New connection from %s", inet_ntoa(cli_addr.sin_addr));

        ClientCtx *ctx = calloc(1, sizeof(ClientCtx));
        ctx->sock    = cli_sock;
        ctx->user_id = -1;

        pthread_t tid;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_create(&tid, &attr, client_thread, ctx);
        pthread_attr_destroy(&attr);
    }
    close(server_fd);
}