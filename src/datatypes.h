#include <pebble.h>
#ifndef DATATYPES_H
#define DATATYPES_H

typedef int32_t caltime_t; //in format: minutes + 60*hours + 60*24*weekday + 60*24*7*dayOfMonth + 60*24*7*32*(month-1) + 60*24*7*32*12*(year-1900). It holds that weekday == 0 <=> monday
//This format is good for (way) longer than 2038. Also, PebbleOS 2.0 does not support time manipulation at the moment,
//so something else was needed. caltime_t has all necessary information.
//The format preserves natural ordering of points in time. 
//However, no time arithmetic should be done one this directly. There is nothing accounting even for the number of days in a certain month...

typedef struct { //should be <256 bytes for persist reasons
	char title[30];
	char location[30];
	bool all_day;
	
	caltime_t start_time; 
	caltime_t end_time;
} CalendarEvent;

//For comments, see datatypes.c
CalendarEvent* create_calendar_event();
void cal_set_title_and_loc(CalendarEvent* event, char* title, char* loc); //strings will be (deep-)copied
void cal_set_allday(CalendarEvent* event, bool allday);
void cal_set_start_time(CalendarEvent* event, caltime_t time);
void cal_set_end_time(CalendarEvent* event, caltime_t time);

bool cal_ends_before(CalendarEvent* before, CalendarEvent* after);
bool cal_ends_before_tm(CalendarEvent* before, struct tm *after);
bool cal_begins_before_tomorrow(CalendarEvent* event);
bool cal_begins_tomorrow(CalendarEvent* event);
bool cal_begins_later_day(CalendarEvent* before, CalendarEvent* after);

caltime_t tm_to_caltime(struct tm *t);
caltime_t tm_to_caltime_date_only(struct tm *t);

caltime_t caltime_to_date_only(caltime_t t);

int32_t caltime_get_minute(caltime_t t);
int32_t caltime_get_hour(caltime_t t);
int32_t caltime_get_weekday(caltime_t t);
int32_t caltime_get_day(caltime_t t);
int32_t caltime_get_month(caltime_t t);
int32_t caltime_get_year(caltime_t t);
caltime_t caltime_get_tomorrow(caltime_t t);
int caltime_month_num_days(caltime_t t);
#endif
