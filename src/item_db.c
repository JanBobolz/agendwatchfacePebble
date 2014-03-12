#include <pebble.h>
#include <item_db.h>
#include <datatypes.h>
#include <persist_const.h>

//Maximal number of items this database can store. Should be small enough so persistence memory is not exhausted (also, phone has a limit of items it wants to send, this should correspond to this constant)
#define NUM_EVENTS_SAVED 10

AgendaItem *db_items[NUM_EVENTS_SAVED]; //the 'database' itself
int current_num_elems = 0; //number of actual entries in db_events
bool dirty_bit = 0; //1 if there were changes to the database since last persist

void db_reset() { //empties database. Also good to call to tidy up occupied heap space
	for (int i=0; i<current_num_elems; i++)
		free(db_items[i]);
	
	dirty_bit = 1;
	current_num_elems = 0;
}

int db_size() { //number of elements in the database
	return current_num_elems;
}

void db_put(AgendaItem* item){ //inserts item into database. Associated heap memory for event will now be managed by the db.
	if (current_num_elems >= NUM_EVENTS_SAVED)
		return;
	
	dirty_bit = 1;
	db_items[current_num_elems++] = item;
}

AgendaItem* db_get(const int offset) { //gives access to the offset'th item (zero based)
	if (offset >= current_num_elems)
		return 0;
	return db_items[offset];
}

void db_persist() { //saves database into persistent storage.
	if (!dirty_bit)
		return;
	
	persist_write_int(PERSIST_NUM_ELEMS, current_num_elems);
	for (int i=0;i<current_num_elems;i++)
		persist_write_data(PERSIST_DB_PREFIX|i, db_items[i], sizeof(AgendaItem));
}

void db_restore_persisted() { //restores database from persistent storage. Please clear db beforehand if nonempty
	if (!persist_exists(PERSIST_NUM_ELEMS) || current_num_elems != 0)
		return;
	
	current_num_elems = persist_read_int(PERSIST_NUM_ELEMS);
	if (current_num_elems < 0) { //error code
		current_num_elems = 0;
		return;
	}

	for (int i=0;i<current_num_elems;i++) {
		db_items[i] = malloc(sizeof(AgendaItem));
		if (persist_read_data(PERSIST_DB_PREFIX|i, db_items[i], sizeof(AgendaItem)) < 0) {
			current_num_elems = i;
			return;
		}
	}
}