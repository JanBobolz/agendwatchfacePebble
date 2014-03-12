#include <pebble.h>
#include <settings.h>
#include <datatypes.h>
#ifndef ITEM_DB_H
#define ITEM_DB_H	

void db_reset(); //empties database. Also good to call to tidy up heap space
void db_put(AgendaItem* event); //inserts item into database. Associated heap memory for item will now be managed by the db.
AgendaItem* db_get(const int offset); //gives access to the offset'th item (zero based). Returns 0 if no more entries are available
int db_size(); //returns number of items in the db
void db_persist(); //saves database into persistent storage.
void db_restore_persisted(); //restores database from persistent storage.

#endif