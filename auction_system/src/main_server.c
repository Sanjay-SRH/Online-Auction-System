/*
 * main_server.c
 * Entry point for the auction server.
 * Seeds default admin account on first run.
 */
#include "common.h"
#include "logger.h"
#include "user_db.h"
#include "item_db.h"
#include "bid_engine.h"
#include "ipc.h"
#include "server.h"

static void seed_default_admin(void) {
    if (userdb_find_by_name("admin")) return; /* already seeded */
    User admin = {0};
    strncpy(admin.username, "admin",   sizeof(admin.username)  - 1);
    strncpy(admin.password, "admin123", sizeof(admin.password) - 1);
    admin.role   = ROLE_ADMIN;
    admin.active = 1;
    admin.balance = 0.0;
    userdb_add(&admin);

    User auctioneer = {0};
    strncpy(auctioneer.username, "seller1",   sizeof(auctioneer.username) - 1);
    strncpy(auctioneer.password, "seller123", sizeof(auctioneer.password) - 1);
    auctioneer.role   = ROLE_AUCTIONEER;
    auctioneer.active = 1;
    userdb_add(&auctioneer);

    User bidder = {0};
    strncpy(bidder.username, "bidder1",   sizeof(bidder.username) - 1);
    strncpy(bidder.password, "bidder123", sizeof(bidder.password) - 1);
    bidder.role          = ROLE_BIDDER;
    bidder.active        = 1;
    bidder.need_pw_change = 1;   /* first-login password change required */
    userdb_add(&bidder);

    LOG_INFO("Default accounts seeded (admin/admin123, seller1/seller123, bidder1/bidder123)");
}

int main(void) {
    logger_init();
    LOG_INFO("=== Online Auction System – Server Starting ===");

    /* Create data directories if absent */
    system("mkdir -p data logs");

    userdb_load();
    itemdb_load();
    bid_load();

    seed_default_admin();

    ipc_signal_setup();
    ipc_shm_init();

    server_run();   /* blocks until Ctrl-C */

    ipc_shm_destroy();
    return 0;
}