#include <pebble.h>
#include <event_db.h>
#include <datatypes.h>
#include <persist_const.h>

//Maximal number of events this database can store. Should be small enough so persistence memory is not exhausted (also, phone has a limit of events it wants to send, this should correspond to this constant)
#define NUM_EVENTS_SAVED 10

CalendarEvent *db_events[NUM_EVENTS_SAVED]; //the 'database' itself
int current_num_elems = 0; //number of actual entries in db_events

void event_db_reset() { //empties database. Also good to call to tidy up occupied heap space
	for (int i=0; i<current_num_elems; i++)
		free(db_events[i]);
	
	current_num_elems = 0;
}

int event_db_size() { //number of elements in the database
	return current_num_elems;
}

void event_db_put(CalendarEvent* event){ //inserts event into database. Associated heap memory for event will now be managed by the db.
	if (current_num_elems >= NUM_EVENTS_SAVED)
		return;
	
	db_events[current_num_elems++] = event;
	//APP_LOG(APP_LOG_LEVEL_DEBUG, "Added event %s to database", event->title);
}

CalendarEvent* event_db_get(const int offset) { //gives access to the offset'th CalendarEvent (zero based)
	if (offset >= current_num_elems)
		return 0;
	return db_events[offset];
}

void event_db_persist() { //saves database into persistent storage.
	persist_write_int(PERSIST_NUM_ELEMS, current_num_elems);
	for (int i=0;i<current_num_elems;i++)
		persist_write_data(PERSIST_CAL_PREFIX|i, db_events[i], sizeof(CalendarEvent));
}

void event_db_restore_persisted() { //restores database from persistent storage. Please clear db beforehand if nonempty
	if (!persist_exists(PERSIST_NUM_ELEMS) || current_num_elems != 0)
		return;
	
	current_num_elems = persist_read_int(PERSIST_NUM_ELEMS);
	if (current_num_elems < 0) { //error code
		current_num_elems = 0;
		return;
	}

	for (int i=0;i<current_num_elems;i++) {
		db_events[i] = malloc(sizeof(CalendarEvent));
		if (persist_read_data(PERSIST_CAL_PREFIX|i, db_events[i], sizeof(CalendarEvent)) < 0) {
			current_num_elems = i;
			return;
		}
		//APP_LOG(APP_LOG_LEVEL_DEBUG, "Got persisted event %s in database", db_events[i]->title);
	}
}