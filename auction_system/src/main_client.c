/*
 * main_client.c
 * Online Auction System – Interactive CLI Client
 *
 * Flow:
 *   1. Show Login Screen
 *   2. Server returns role + need_pw_change flag
 *   3. If bidder AND need_pw_change == 1  →  force password change screen
 *   4. Route to the correct role dashboard:
 *        ROLE_ADMIN      →  Admin Dashboard
 *        ROLE_AUCTIONEER →  Auctioneer Dashboard
 *        ROLE_BIDDER     →  Bidder Dashboard
 *
 * Satisfies: Socket Programming – Client side (4.5)
 */
#include "common.h"
#include "server.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <termios.h>

static int  g_sock    = -1;
static int  g_role    = ROLE_GUEST;
static int  g_user_id = -1;
static char g_username[MAX_USERNAME] = {0};

/* ════════════════════════════════════════════════════
 *  Utility helpers
 * ════════════════════════════════════════════════════ */

static char *read_line(char *buf, int sz) {
    if (!fgets(buf, sz, stdin)) { buf[0] = '\0'; return buf; }
    buf[strcspn(buf, "\n")] = '\0';
    return buf;
}

static void read_password(char *buf, int sz) {
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(tcflag_t)ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    read_line(buf, sz);
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    printf("\n");
}

static void pause_for_user(void) {
    printf("\n  Press [Enter] to continue...");
    fflush(stdout);
    char dummy[8];
    read_line(dummy, sizeof(dummy));
}

static Message do_request(MsgType type, const char *payload) {
    Message msg  = {0};
    Message resp = {0};
    msg.type = type;
    if (payload) strncpy(msg.payload, payload, sizeof(msg.payload) - 1);
    if (send(g_sock, &msg, sizeof(msg), 0) <= 0) {
        snprintf(resp.payload, sizeof(resp.payload), "[Network error: send failed]");
        resp.status = -99;
        return resp;
    }
    if (recv(g_sock, &resp, sizeof(resp), 0) <= 0) {
        snprintf(resp.payload, sizeof(resp.payload), "[Network error: no response]");
        resp.status = -99;
    }
    return resp;
}

static void clear_screen(void) { printf("\033[2J\033[H"); }
static void print_ok(const char *msg)   { printf("  \033[32m[OK]  %s\033[0m\n", msg); }
static void print_err(const char *msg)  { printf("  \033[31m[ERR] %s\033[0m\n", msg); }
static void print_info(const char *msg) { printf("  \033[36m[?]   %s\033[0m\n", msg); }
static void print_divider(void) {
    printf("  ──────────────────────────────────────────────────────\n");
}

/* ── Validated numeric input helpers ── */

/* Read an integer strictly in [min_val, max_val]; loop until valid. */
static int get_bounded_int(const char *prompt, int min_val, int max_val) {
    char buf[32];
    while (1) {
        printf("%s", prompt);
        fflush(stdout);
        read_line(buf, sizeof(buf));
        if (buf[0] == '\0') {
            printf("  \033[31m[ERR] Input cannot be empty. Enter a number between %d and %d.\033[0m\n",
                   min_val, max_val);
            continue;
        }
        int val = atoi(buf);
        if (val >= min_val && val <= max_val) return val;
        printf("  \033[31m[ERR] Invalid input '%s'. Please enter a number between %d and %d.\033[0m\n",
               buf, min_val, max_val);
    }
}

/* Read a positive integer (>0); loop until valid. */
static int get_positive_int(const char *prompt) {
    char buf[32];
    while (1) {
        printf("%s", prompt);
        fflush(stdout);
        read_line(buf, sizeof(buf));
        int val = atoi(buf);
        if (val > 0) return val;
        printf("  \033[31m[ERR] Please enter a positive number (> 0).\033[0m\n");
    }
}

/* Read a non-negative integer (>=0); loop until valid. */
static int get_nonneg_int(const char *prompt) {
    char buf[32];
    while (1) {
        printf("%s", prompt);
        fflush(stdout);
        read_line(buf, sizeof(buf));
        /* Require at least one digit character so empty/garbage fails */
        int has_digit = 0;
        for (int i = 0; buf[i]; i++) if (buf[i] >= '0' && buf[i] <= '9') { has_digit = 1; break; }
        int val = atoi(buf);
        if (has_digit && val >= 0) return val;
        printf("  \033[31m[ERR] Please enter 0 or a positive number.\033[0m\n");
    }
}

/* Read a positive double (>0.0); loop until valid. */
static double get_positive_double(const char *prompt) {
    char buf[32];
    while (1) {
        printf("%s", prompt);
        fflush(stdout);
        read_line(buf, sizeof(buf));
        double val = atof(buf);
        if (val > 0.0) return val;
        printf("  \033[31m[ERR] Please enter a positive value (> 0).\033[0m\n");
    }
}

/* Read a non-negative double (>=0.0, e.g. "keep current" uses 0). */
static double get_nonneg_double(const char *prompt) {
    char buf[32];
    while (1) {
        printf("%s", prompt);
        fflush(stdout);
        read_line(buf, sizeof(buf));
        int has_digit = 0;
        for (int i = 0; buf[i]; i++) if (buf[i] >= '0' && buf[i] <= '9') { has_digit = 1; break; }
        double val = atof(buf);
        if (has_digit && val >= 0.0) return val;
        printf("  \033[31m[ERR] Please enter 0 or a positive value.\033[0m\n");
    }
}

static void print_top_bar(const char *title, const char *role_label, const char *colour) {
    clear_screen();
    printf("%s", colour);
    printf("  ╔══════════════════════════════════════════════════════════╗\n");
    printf("  ║           ONLINE AUCTION SYSTEM  -  %-20s║\n", title);
    printf("  ╠══════════════════════════════════════════════════════════╣\n");
    printf("  ║  Logged in as : %-14s  Role : %-18s║\n", g_username, role_label);
    printf("  ╚══════════════════════════════════════════════════════════╝\n");
    printf("\033[0m\n");
}

static int get_choice(int max) {
    char buf[16];
    while (1) {
        printf("\n  Enter choice [1-%d, 0=Logout]: ", max);
        fflush(stdout);
        read_line(buf, sizeof(buf));
        /* Reject empty input and non-numeric strings (e.g. "abc" -> atoi=0 -> wrongly logs out) */
        if (buf[0] == '\0' || buf[0] < '0' || buf[0] > '9') {
            printf("  \033[31m[ERR] Invalid input '%s'. Please enter a number between 0 and %d.\033[0m\n",
                   buf, max);
            continue;
        }
        int val = atoi(buf);
        if (val >= 0 && val <= max)
            return val;
        printf("  \033[31m[ERR] '%s' is not a valid option. Please choose between 0 and %d.\033[0m\n",
               buf, max);
    }
}

/* ════════════════════════════════════════════════════
 *  LOGIN SCREEN
 * ════════════════════════════════════════════════════ */

static int login_screen(void) {
    char uname[MAX_USERNAME];
    char pass[MAX_PASSWORD];

    while (1) {
        clear_screen();
        printf("\033[33m");
        printf("  +-----------------------------------------+\n");
        printf("  |        ONLINE AUCTION SYSTEM            |\n");
        printf("  |         Please Login to Continue        |\n");
        printf("  +-----------------------------------------+\n");
        printf("\033[0m\n");

        printf("  Username : "); fflush(stdout);
        read_line(uname, sizeof(uname));
        if (uname[0] == '\0') { printf("\n  Exiting...\n"); return 0; }

        printf("  Password : "); fflush(stdout);
        read_password(pass, sizeof(pass));

        char payload[256];
        snprintf(payload, sizeof(payload), "username=%s password=%s", uname, pass);
        Message resp = do_request(MSG_LOGIN, payload);

        if (resp.status != 0) {
            print_err(resp.payload);
            pause_for_user();
            continue;
        }

        /* Parse: role=N need_pw_change=N username=X ... */
        int  role_val = ROLE_GUEST, need_pw = 0, srv_uid = -1;
        char srv_uname[MAX_USERNAME] = {0};
        sscanf(resp.payload,
               "role=%d need_pw_change=%d username=%31s",
               &role_val, &need_pw, srv_uname);
        /* Also try to parse user_id if server sends it */
        char *uid_p = strstr(resp.payload, "user_id=");
        if (uid_p) sscanf(uid_p, "user_id=%d", &srv_uid);

        g_role    = role_val;
        g_user_id = srv_uid;
        strncpy(g_username,
                srv_uname[0] ? srv_uname : uname,
                MAX_USERNAME - 1);

        /* ── Bidder: forced first-login password change ── */
        if (g_role == ROLE_BIDDER && need_pw) {
            clear_screen();
            printf("\033[35m");
            printf("  +--------------------------------------------------+\n");
            printf("  |   SECURITY NOTICE - Password Change Required     |\n");
            printf("  |   You must set a new password before accessing   |\n");
            printf("  |   your dashboard. (Default passwords are unsafe) |\n");
            printf("  +--------------------------------------------------+\n");
            printf("\033[0m\n");

            char new_pass[MAX_PASSWORD], confirm[MAX_PASSWORD];
            while (1) {
                printf("  New Password     : "); fflush(stdout);
                read_password(new_pass, sizeof(new_pass));
                printf("  Confirm Password : "); fflush(stdout);
                read_password(confirm, sizeof(confirm));

                if (strcmp(new_pass, confirm) != 0) {
                    print_err("Passwords do not match. Try again.");
                    continue;
                }
                if (strlen(new_pass) < 4) {
                    print_err("Password must be at least 4 characters.");
                    continue;
                }
                char pw_payload[128];
                snprintf(pw_payload, sizeof(pw_payload),
                         "new_password=%s", new_pass);
                Message pw_resp = do_request(MSG_CHANGE_PASSWORD, pw_payload);
                if (pw_resp.status == 0) {
                    print_ok("Password changed! Loading your dashboard...");
                    pause_for_user();
                    break;
                } else {
                    print_err(pw_resp.payload);
                }
            }
        }

        return 1;
    }
}

/* ════════════════════════════════════════════════════
 *  ADMIN DASHBOARD  (red theme)
 * ════════════════════════════════════════════════════ */

static void admin_dashboard(void) {
    int  choice;

    while (1) {
        print_top_bar("ADMIN DASHBOARD", "Administrator", "\033[31m");

        printf("  +-----------------------------------+\n");
        printf("  |   USER MANAGEMENT                 |\n");
        printf("  |   1. List all users               |\n");
        printf("  |   2. Add new user                 |\n");
        printf("  |   3. Disable a user               |\n");
        printf("  +-----------------------------------+\n");
        printf("  |   AUCTION MANAGEMENT              |\n");
        printf("  |   4. List ALL items (all status)  |\n");
        printf("  |   5. List active auctions         |\n");
        printf("  |   6. Force-close an auction       |\n");
        printf("  |   7. View bids on an item         |\n");
        printf("  +-----------------------------------+\n");
        printf("  |   ACCOUNT                         |\n");
        printf("  |   8. Change my password           |\n");
        printf("  |   0. Logout                       |\n");
        printf("  +-----------------------------------+\n");

        choice = get_choice(8);

        switch (choice) {
        case 0:
            do_request(MSG_LOGOUT, NULL);
            return;

        case 1: {
            Message r = do_request(MSG_LIST_USERS, NULL);
            print_divider();
            printf("  ID   Username         Role         Status\n");
            print_divider();
            printf("%s\n", r.payload);
            pause_for_user();
            break;
        }

        case 2: {
            char uname[MAX_USERNAME], pass[MAX_PASSWORD];
            printf("\n  New Username  : "); read_line(uname, sizeof(uname));
            printf("  Password      : "); read_password(pass, sizeof(pass));
            int role_int = get_bounded_int(
                "  Role (0=Admin  1=Auctioneer  2=Bidder): ", 0, 2);
            char payload[256];
            snprintf(payload, sizeof(payload),
                     "username=%s password=%s role=%d", uname, pass, role_int);
            Message r = do_request(MSG_ADD_USER, payload);
            r.status == 0 ? print_ok(r.payload) : print_err(r.payload);
            pause_for_user();
            break;
        }

        case 3: {
            int uid = get_positive_int("\n  User ID to disable: ");
            char payload[64];
            snprintf(payload, sizeof(payload), "user_id=%d", uid);
            Message r = do_request(MSG_DISABLE_USER, payload);
            r.status == 0 ? print_ok(r.payload) : print_err(r.payload);
            pause_for_user();
            break;
        }

        case 4: {
            Message r = do_request(MSG_LIST_ALL_ITEMS, NULL);
            print_divider();
            printf("  ALL AUCTION ITEMS (Pending / Active / Sold / Expired)\n");
            print_divider();
            printf("%s\n", r.payload);
            pause_for_user();
            break;
        }

        case 5: {
            Message r = do_request(MSG_LIST_ITEMS, NULL);
            print_divider();
            printf("%s\n", r.payload);
            pause_for_user();
            break;
        }

        case 6: {
            int iid = get_positive_int("\n  Item ID to close: ");
            char payload[64];
            snprintf(payload, sizeof(payload), "item_id=%d", iid);
            Message r = do_request(MSG_CLOSE_ITEM, payload);
            r.status == 0 ? print_ok(r.payload) : print_err(r.payload);
            pause_for_user();
            break;
        }

        case 7: {
            int iid = get_positive_int("\n  Item ID: ");
            char payload[64];
            snprintf(payload, sizeof(payload), "item_id=%d", iid);
            Message r = do_request(MSG_VIEW_BIDS, payload);
            print_divider();
            printf("%s\n", r.payload);
            pause_for_user();
            break;
        }

        case 8: {
            char new_pass[MAX_PASSWORD], confirm[MAX_PASSWORD];
            printf("\n  New Password     : "); read_password(new_pass, sizeof(new_pass));
            printf("  Confirm Password : "); read_password(confirm, sizeof(confirm));
            if (strcmp(new_pass, confirm) != 0) {
                print_err("Passwords do not match.");
            } else {
                char payload[128];
                snprintf(payload, sizeof(payload), "new_password=%s", new_pass);
                Message r = do_request(MSG_CHANGE_PASSWORD, payload);
                r.status == 0 ? print_ok(r.payload) : print_err(r.payload);
            }
            pause_for_user();
            break;
        }

        default:
            print_err("Invalid choice. Please enter a number from the menu.");
            pause_for_user();
        }
    }
}

/* ════════════════════════════════════════════════════
 *  AUCTIONEER DASHBOARD  (blue theme)
 * ════════════════════════════════════════════════════ */

static void auctioneer_dashboard(void) {
    int  choice;

    while (1) {
        print_top_bar("AUCTIONEER DASHBOARD", "Auctioneer", "\033[34m");

        printf("  +-----------------------------------+\n");
        printf("  |   MY AUCTIONS                     |\n");
        printf("  |   1. List ALL items (all status)  |\n");
        printf("  |   2. List active auctions         |\n");
        printf("  |   3. Add new auction item(s)      |\n");
        printf("  |   4. Remove a pending item        |\n");
        printf("  |   5. Update item price/increment  |\n");
        printf("  |   6. Start auction for an item    |\n");
        printf("  |   7. Close one of my auctions     |\n");
        printf("  |   8. View bids on an item         |\n");
        printf("  +-----------------------------------+\n");
        printf("  |   ACCOUNT                         |\n");
        printf("  |   9. Change my password           |\n");
        printf("  |   0. Logout                       |\n");
        printf("  +-----------------------------------+\n");

        choice = get_choice(9);

        switch (choice) {
        case 0:
            do_request(MSG_LOGOUT, NULL);
            return;

        case 1: {
            Message r = do_request(MSG_LIST_ALL_ITEMS, NULL);
            print_divider();
            printf("  ALL AUCTION ITEMS (Pending / Active / Sold / Expired)\n");
            print_divider();
            printf("%s\n", r.payload);
            pause_for_user();
            break;
        }

        case 2: {
            Message r = do_request(MSG_LIST_ITEMS, NULL);
            print_divider();
            printf("%s\n", r.payload);
            pause_for_user();
            break;
        }

        case 3: {
            int total = get_nonneg_int("\n  How many items do you want to add? (enter 0 to cancel): ");
            if (total == 0) { print_info("Cancelled."); pause_for_user(); break; }

            int added = 0;
            for (int n = 1; n <= total; n++) {
                printf("\n  ── Item %d of %d (enter 0 for Name to stop early) ──\n", n, total);
                char name[MAX_ITEM_NAME], desc[256];

                printf("  Item Name          : "); read_line(name, sizeof(name));
                if (strcmp(name, "0") == 0) { print_info("Stopped early."); break; }

                printf("  Description        : "); read_line(desc, sizeof(desc));
                double price     = get_positive_double("  Starting Price ($) : ");
                double increment = get_positive_double("  Min Increment ($)  : ");

                char payload[512];
                snprintf(payload, sizeof(payload),
                         "name=%s price=%.2f increment=%.2f desc=%s",
                         name, price, increment, desc);
                Message r = do_request(MSG_ADD_ITEM, payload);
                r.status == 0 ? print_ok(r.payload) : print_err(r.payload);
                if (r.status == 0) added++;
            }
            printf("\n  %d item(s) added.\n", added);
            pause_for_user();
            break;
        }

        case 4: {
            /* Remove a pending item */
            Message list = do_request(MSG_LIST_ALL_ITEMS, NULL);
            print_divider();
            printf("%s\n", list.payload);
            print_divider();
            int iid = get_positive_int("\n  Item ID to remove (must be PENDING): ");
            char payload[64];
            snprintf(payload, sizeof(payload), "item_id=%d", iid);
            Message r = do_request(MSG_REMOVE_ITEM, payload);
            r.status == 0 ? print_ok(r.payload) : print_err(r.payload);
            pause_for_user();
            break;
        }

        case 5: {
            /* Update price/increment of a pending item */
            Message list = do_request(MSG_LIST_ALL_ITEMS, NULL);
            print_divider();
            printf("%s\n", list.payload);
            print_divider();
            int item_id = get_positive_int("\n  Item ID to update (must be PENDING): ");
            double new_price = get_nonneg_double("  New Starting Price ($) (0 = keep current): ");
            double new_inc   = get_nonneg_double("  New Min Increment ($)  (0 = keep current): ");
            char payload[128];
            snprintf(payload, sizeof(payload),
                     "item_id=%d price=%.2f increment=%.2f", item_id, new_price, new_inc);
            Message r = do_request(MSG_UPDATE_ITEM_PRICE, payload);
            r.status == 0 ? print_ok(r.payload) : print_err(r.payload);
            pause_for_user();
            break;
        }

        case 6: {
            int item_id      = get_positive_int("\n  Item ID to start   : ");
            double duration  = get_positive_double("  Duration (seconds) : ");
            char payload[64];
            snprintf(payload, sizeof(payload), "item_id=%d duration=%.0f", item_id, duration);
            Message r = do_request(MSG_START_AUCTION, payload);
            r.status == 0 ? print_ok(r.payload) : print_err(r.payload);
            pause_for_user();
            break;
        }

        case 7: {
            int iid = get_positive_int("\n  Item ID to close: ");
            char payload[64];
            snprintf(payload, sizeof(payload), "item_id=%d", iid);
            Message r = do_request(MSG_CLOSE_ITEM, payload);
            r.status == 0 ? print_ok(r.payload) : print_err(r.payload);
            pause_for_user();
            break;
        }

        case 8: {
            int iid = get_positive_int("\n  Item ID: ");
            char payload[64];
            snprintf(payload, sizeof(payload), "item_id=%d", iid);
            Message r = do_request(MSG_VIEW_BIDS, payload);
            print_divider();
            printf("%s\n", r.payload);
            pause_for_user();
            break;
        }

        case 9: {
            char new_pass[MAX_PASSWORD], confirm[MAX_PASSWORD];
            printf("\n  New Password     : "); read_password(new_pass, sizeof(new_pass));
            printf("  Confirm Password : "); read_password(confirm, sizeof(confirm));
            if (strcmp(new_pass, confirm) != 0) {
                print_err("Passwords do not match.");
            } else {
                char payload[128];
                snprintf(payload, sizeof(payload), "new_password=%s", new_pass);
                Message r = do_request(MSG_CHANGE_PASSWORD, payload);
                r.status == 0 ? print_ok(r.payload) : print_err(r.payload);
            }
            pause_for_user();
            break;
        }

        default:
            print_err("Invalid choice. Please enter a valid option from the menu.");
            pause_for_user();
        }
    }
}


/* ════════════════════════════════════════════════════
 *  BIDDER DASHBOARD  (green theme)
 * ════════════════════════════════════════════════════ */

static void bidder_dashboard(void) {
    int  choice;

    /* Build role label showing user id */
    char role_label[32];
    if (g_user_id > 0)
        snprintf(role_label, sizeof(role_label), "Bidder (ID #%d)", g_user_id);
    else
        snprintf(role_label, sizeof(role_label), "Bidder");

    while (1) {
        print_top_bar("BIDDER DASHBOARD", role_label, "\033[32m");

        printf("  +-----------------------------------+\n");
        printf("  |   AUCTIONS                        |\n");
        printf("  |   1. List ALL items (all status)  |\n");
        printf("  |   2. Browse active auctions       |\n");
        printf("  |   3. Place a bid                  |\n");
        printf("  |   4. View bids on an item         |\n");
        printf("  |   5. My bids (won/lost/ongoing)   |\n");
        printf("  +-----------------------------------+\n");
        printf("  |   ACCOUNT                         |\n");
        printf("  |   6. Change my password           |\n");
        printf("  |   0. Logout                       |\n");
        printf("  +-----------------------------------+\n");

        choice = get_choice(6);

        switch (choice) {
        case 0:
            do_request(MSG_LOGOUT, NULL);
            return;

        case 1: {
            Message r = do_request(MSG_LIST_ALL_ITEMS, NULL);
            print_divider();
            printf("  ALL AUCTION ITEMS (Pending / Active / Sold / Expired)\n");
            print_divider();
            printf("%s\n", r.payload);
            pause_for_user();
            break;
        }

        case 2: {
            Message r = do_request(MSG_LIST_ITEMS, NULL);
            print_divider();
            printf("%s\n", r.payload);
            pause_for_user();
            break;
        }

        case 3: {
            int item_id = get_positive_int("\n  Item ID   : ");

            /* Show current bids for reference */
            char info_payload[64];
            snprintf(info_payload, sizeof(info_payload), "item_id=%d", item_id);
            Message bids = do_request(MSG_VIEW_BIDS, info_payload);
            print_divider();
            printf("%s", bids.payload);
            print_divider();

            /* Fetch item list to show min increment */
            Message items = do_request(MSG_LIST_ITEMS, NULL);
            char min_hint[64] = "";
            char search[32];
            snprintf(search, sizeof(search), "[%d]", item_id);
            char *found = strstr(items.payload, search);
            if (found) {
                char *mr = strstr(found, "Min Raise   : $");
                if (mr) {
                    double min_raise = 0;
                    sscanf(mr, "Min Raise   : $%lf", &min_raise);
                    snprintf(min_hint, sizeof(min_hint), " (min raise: $%.2f)", min_raise);
                }
            }

            char bid_prompt[128];
            snprintf(bid_prompt, sizeof(bid_prompt), "  Your Bid ($)%s: ", min_hint);
            double amount = get_positive_double(bid_prompt);

            char payload[128];
            snprintf(payload, sizeof(payload),
                     "item_id=%d amount=%.2f", item_id, amount);
            Message r = do_request(MSG_PLACE_BID, payload);
            r.status == 0 ? print_ok(r.payload) : print_err(r.payload);
            pause_for_user();
            break;
        }

        case 4: {
            int iid = get_positive_int("\n  Item ID: ");
            char payload[64];
            snprintf(payload, sizeof(payload), "item_id=%d", iid);
            Message r = do_request(MSG_VIEW_BIDS, payload);
            print_divider();
            printf("%s\n", r.payload);
            pause_for_user();
            break;
        }

        case 5: {
            Message r = do_request(MSG_MY_BIDS, NULL);
            print_divider();
            printf("  MY BIDS — Won / Lost / Ongoing\n");
            print_divider();
            printf("%s\n", r.payload);
            pause_for_user();
            break;
        }

        case 6: {
            char new_pass[MAX_PASSWORD], confirm[MAX_PASSWORD];
            printf("\n  New Password     : "); read_password(new_pass, sizeof(new_pass));
            printf("  Confirm Password : "); read_password(confirm, sizeof(confirm));
            if (strcmp(new_pass, confirm) != 0) {
                print_err("Passwords do not match.");
            } else if (strlen(new_pass) < 4) {
                print_err("Password too short (min 4 chars).");
            } else {
                char payload[128];
                snprintf(payload, sizeof(payload), "new_password=%s", new_pass);
                Message r = do_request(MSG_CHANGE_PASSWORD, payload);
                r.status == 0 ? print_ok(r.payload) : print_err(r.payload);
            }
            pause_for_user();
            break;
        }

        default:
            print_err("Invalid choice. Please enter a valid option from the menu.");
            pause_for_user();
        }
    }
}

/* ════════════════════════════════════════════════════
 *  MAIN
 * ════════════════════════════════════════════════════ */

int main(int argc, char *argv[]) {
    const char *host = (argc > 1) ? argv[1] : "127.0.0.1";

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(SERVER_PORT);
    inet_pton(AF_INET, host, &addr.sin_addr);

    /* Login → dashboard → logout → reconnect → login again loop */
    while (1) {
        /* Open a fresh socket for every session */
        g_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (g_sock < 0) { perror("socket"); return 1; }

        if (connect(g_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            fprintf(stderr,
                    "\033[31mCannot connect to %s:%d - is the server running?\033[0m\n",
                    host, SERVER_PORT);
            return 1;
        }

        if (!login_screen()) {
            close(g_sock);
            break;
        }

        switch (g_role) {
            case ROLE_ADMIN:      admin_dashboard();      break;
            case ROLE_AUCTIONEER: auctioneer_dashboard(); break;
            case ROLE_BIDDER:     bidder_dashboard();     break;
            default:
                print_err("Unknown role - access denied.");
                break;
        }

        close(g_sock);
        g_sock = -1;

        clear_screen();
        printf("\n  You have been logged out.\n");
        printf("  Press [Enter] to log in again, or Ctrl-C to quit.\n");
        char dummy[8];
        fgets(dummy, sizeof(dummy), stdin);
    }

    printf("\nGoodbye!\n");
    return 0;
}