#ifndef ITEM_DB_H
#define ITEM_DB_H

#include "common.h"

int   itemdb_load(void);
int   itemdb_save(void);
int   itemdb_add(Item *item);
Item *itemdb_find(int id);
int   itemdb_update(const Item *item);
int   itemdb_close(int id);
Item *itemdb_get_active(int *count);
Item *itemdb_get_all(int *count);

#endif /* ITEM_DB_H */
