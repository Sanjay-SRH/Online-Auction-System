/*
 * auth.c
 * Role-based authentication and authorization.
 * Satisfies: Role-Based Authorization (4.1)
 */
#include "auth.h"
#include "user_db.h"
#include "logger.h"

User *auth_login(const char *username, const char *password) {
    User *u = userdb_find_by_name(username);
    if (!u) {
        LOG_WARN("Login failed: unknown user '%s'", username);
        return NULL;
    }
    if (!u->active) {
        LOG_WARN("Login failed: disabled user '%s'", username);
        return NULL;
    }
    if (strcmp(u->password, password) != 0) {
        LOG_WARN("Login failed: bad password for '%s'", username);
        return NULL;
    }
    LOG_INFO("User '%s' logged in (role=%s)", username, role_str(u->role));
    return u;
}

/*
 * Role hierarchy: Admin > Auctioneer > Bidder > Guest
 * Lower enum value = higher privilege.
 */
int auth_check_role(const User *user, Role required) {
    if (!user) return 0;
    return user->role <= required;
}

void auth_log(const char *event, const User *user) {
    if (user)
        LOG_INFO("AUTH EVENT: %s | user=%s role=%s",
                 event, user->username, role_str(user->role));
    else
        LOG_INFO("AUTH EVENT: %s | user=(none)", event);
}
