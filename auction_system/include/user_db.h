#ifndef USER_DB_H
#define USER_DB_H

#include "common.h"

int  userdb_load(void);
int  userdb_save(void);
int  userdb_add(const User *u);
User *userdb_find_by_id(int id);
User *userdb_find_by_name(const char *username);
int  userdb_disable(int id);
int  userdb_change_password(int id, const char *new_pass);
int  userdb_count(void);
User *userdb_get_all(int *count);

#endif /* USER_DB_H */
