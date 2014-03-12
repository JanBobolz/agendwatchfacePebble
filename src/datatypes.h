#include <pebble.h>
#ifndef DATATYPES_H
#define DATATYPES_H

//design value for "hide the row"
#define ROW_DESIGN_HIDE 0
//the first of three bits that make up the constant (from Android TimeDisplayType enum) what kind of times should be shown
#define ROW_DESIGN_TIME_TYPE_OFFSET 0x02
#define ROW_DESIGN_TIME_COUNTDOWN 0x10
#define ROW_DESIGN_TEXT_BOLD 0x20

typedef int32_t caltime_t; //in format: minutes + 60*hours + 60*24*weekday + 60*24*7*dayOfMonth + 60*24*7*32*(month-1) + 60*24*7*32*12*(year-1900). It holds that weekday == 0 <=> monday
//This format is good for (way) longer than 2038. Also, PebbleOS 2.0 does not support time manipulation at the moment,
//so something else was needed. caltime_t has all necessary information.
//The format preserves natural ordering of points in time. 
//However, no time arithmetic should be done one this directly. There is nothing accounting even for the number of days in a certain month...

typedef struct {
	char row1text[30];
	char row2text[30];
	
	uint8_t row1design, row2design;
	
	caltime_t start_time;
	caltime_t end_time;
} AgendaItem;

//For comments, see datatypes.c
AgendaItem* create_agenda_item();
void set_item_row1(AgendaItem* item, char* text, uint8_t design);
void set_item_row2(AgendaItem* item, char* text, uint8_t design);
void set_item_times(AgendaItem* item, caltime_t start, caltime_t end);

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
