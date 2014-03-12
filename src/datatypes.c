#include<pebble.h>
#include<datatypes.h>
	
AgendaItem* create_agenda_item() {
	return malloc(sizeof(AgendaItem));
}

//Setters
void set_item_row1(AgendaItem* item, char* text, uint8_t design) {
	strncpy(item->row1text, text, sizeof(item->row1text));
	item->row1text[sizeof(item->row1text)-1] = 0;
	item->row1design = design;
}

void set_item_row2(AgendaItem* item, char* text, uint8_t design) {
	strncpy(item->row2text, text, sizeof(item->row2text));
	item->row2text[sizeof(item->row2text)-1] = 0;
	item->row2design = design;
}

void set_item_times(AgendaItem* item, caltime_t start, caltime_t end) {
	item->start_time = start;
	item->end_time = end;
}


/*void cal_set_title_and_loc(CalendarEvent* event, char* title, char* location) {//strings will be (deep-)copied and truncated if necessary
	strncpy(event->title, title, sizeof(event->title));
	event->title[sizeof(event->title)-1] = 0;
	strncpy(event->location, location, sizeof(event->location));
	event->location[sizeof(event->location)-1] = 0;
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
	time_t t = time(NULL);
	struct tm *today = localtime(&t);
	
	//Add a day to now and normalize...
	//tomorrow->tm_mday++;
	//mktime(tomorrow); //Code crashes... 
	
	return caltime_get_tomorrow(tm_to_caltime_date_only(today)) == caltime_to_date_only(event->start_time);
}

bool cal_begins_later_day(CalendarEvent* before, CalendarEvent* after) { //true iff 'after' begins on a day later than 'before' begins. Compares only date, not time
	return before->start_time - before->start_time%(60*24) < after->start_time - after->start_time%(60*24);
}*/

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

caltime_t caltime_get_tomorrow(const caltime_t time) { //gives a caltime_t for tomorrow relative to t (date only, no time)
	caltime_t t = caltime_to_date_only(time); //normalize to get rid of time of day
	
	//Normalize day, month, and year if we're at the limits
	if (caltime_get_day(t) == caltime_month_num_days(t)) {
		t -= (caltime_get_day(t)-1)*60*24*7; //set day to one
		
		//Check if month overflows
		if (caltime_get_month(t) == 12) {
			t -= 60*24*7*32*11; //set month to January
			t += 60*24*7*32*12; //increment year
		}
		else
			t += 60*24*7*32; //increment month
	}
	else
		t += 60*24*7; //increment day
	
	//Increment day of the week
	if (caltime_get_weekday(t) == 6)
		t -= 60*24*6;
	else
		t += 60*24;
	
	return t;
}

int caltime_month_num_days(caltime_t t) { //Returns the number of days of the current month
	int year;
	switch (caltime_get_month(t)) {
		case 1:
		case 3:
		case 5:
		case 7:
		case 8:
		case 10:
		case 12:
			return 31;
		
		case 4:
		case 6:
		case 9:
		case 11:
			return 30;
				
		case 2:
			year = (int) caltime_get_year(t);
			if (year%400 == 0)
				return 29;
			if (year%100 == 0)
				return 28;
			if (year%4 == 0)
				return 29;
			return 28;
		default:
			return 0;
	}
}