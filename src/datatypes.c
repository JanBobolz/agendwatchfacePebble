#include<pebble.h>
#include<datatypes.h>
	
CalendarEvent* create_calendar_event() {
	return malloc(sizeof(CalendarEvent));
}

//Calendar setters
void cal_set_title_and_loc(CalendarEvent* event, char* title, char* location) {//strings will be (deep-)copied and truncated if necessary
	strncpy(event->title, title, sizeof(event->title));
	event->title[sizeof(event->title)-1] = 0;
	strncpy(event->location, location, sizeof(event->location));
	event->location[sizeof(event->location)-1] = 0;
}

void cal_set_allday(CalendarEvent* event, bool allday) {
	event->all_day = allday ? 1 : 0;
}

void cal_set_start_time(CalendarEvent* event, caltime_t time) {
	event->start_time = time;
}

void cal_set_end_time(CalendarEvent* event, caltime_t time) {
	event->end_time = time;
}


//Time-related predicates
bool cal_ends_before(CalendarEvent* before, CalendarEvent* after) {
	return before->end_time < after->end_time;
}

bool cal_ends_before_tm(CalendarEvent* before, struct tm *after) {
	return before->end_time < tm_to_caltime(after);
}

bool cal_begins_before_tomorrow(CalendarEvent* event) {
	time_t t = time(NULL);
	struct tm *now = localtime(&t);
	
	return caltime_to_date_only(event->start_time) <= tm_to_caltime_date_only(now);
}

bool cal_begins_tomorrow(CalendarEvent* event) {
	return false; //TODO - code below crashes for some reason on Beta 6
	/*time_t t = time(NULL);
	struct tm *tomorrow = localtime(&t);
	
	//Add a day to now and normalize...
	tomorrow->tm_mday++;
	mktime(tomorrow);
	
	bool result = event->start_day == tomorrow->tm_mday && event->start_month == tomorrow->tm_mon+1 && event->start_year == tomorrow->tm_year+1900;
	return result;*/
}

bool cal_begins_later_day(CalendarEvent* before, CalendarEvent* after) { //true iff 'after' begins on a day later than 'before' begins. Compares only date, not time
	return before->start_time - before->start_time%(60*24) < after->start_time - after->start_time%(60*24);
}

caltime_t tm_to_caltime_date_only(struct tm *t) { //creates caltime_t format with only the date specified (so 00:00 o'clock that day)
	return ((t->tm_wday+6)%7)*60*24+t->tm_mday*60*24*7+t->tm_mon*60*24*7*32+t->tm_year*60*24*7*32*12;
}

caltime_t tm_to_caltime(struct tm *t) { //creates caltime_t format from a time (with date and time set)
	return tm_to_caltime_date_only(t)+t->tm_hour*60+t->tm_min;
}

caltime_t caltime_to_date_only(caltime_t t) { //truncates the time-portion of a caltime_t (for comparison of dates of two caltimes)
	return t-(t%(60*24));
}

//Readers/getters for caltime_t data
int32_t caltime_get_minute(caltime_t t) {
	return t%60;
}

int32_t caltime_get_hour(caltime_t t) {
	return (t/60)%24;
}

int32_t caltime_get_weekday(caltime_t t) {
	return (t/(60*24))%7;
}

int32_t caltime_get_day(caltime_t t) {
	return (t/(60*24*7))%32;
}

int32_t caltime_get_month(caltime_t t) {
	return (t/(60*24*7*32))%12+1;
}

int32_t caltime_get_year(caltime_t t) {
	return (t/(60*24*7*32*12))+1900;
}