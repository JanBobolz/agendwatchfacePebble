#include <pebble.h>
#include <settings.h>
#include <datatypes.h>
#ifndef EVENT_DB_H
#define EVENT_DB_H	

void event_db_reset(); //empties database. Also good to call to tidy up heap space
void event_db_put(CalendarEvent* event); //inserts event into database. Associated heap memory for event will now be managed by the db.
CalendarEvent* event_db_get(const int offset); //gives access to the offset'th CalendarEvent (zero based). Returns 0 if no more entries are available
int event_db_size(); //returns number of events in the db
void event_db_persist(); //saves database into persistent storage.
void event_db_restore_persisted(); //restores database from persistent storage.

#endif