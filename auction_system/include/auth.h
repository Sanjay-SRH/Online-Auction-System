#ifndef AUTH_H
#define AUTH_H

#include "common.h"

/* Returns pointer to matched user (NULL on failure). */
User *auth_login(const char *username, const char *password);

/* Returns 1 if the user has the given minimum role. */
int auth_check_role(const User *user, Role required);

/* Logs an auth event to the audit log. */
void auth_log(const char *event, const User *user);

#endif /* AUTH_H */
