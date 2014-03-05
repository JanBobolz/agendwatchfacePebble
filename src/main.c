#include <pebble.h>
#include <event_db.h>
#include <datatypes.h>
#include <communication.h>
#include <settings.h>
#include <persist_const.h>
	
time_t last_sync = 0; //time where the last successful sync happened
uint8_t last_sync_id = 0; //id that the phone supplied for the last successful sync
caltime_t refresh_at = 0; //time where the event display should be refreshed next (actually, refresh will happen if this threshold lays in the past (so, next minute))

int num_events = 0; //number of events displayed. As many elements will be in the arrays below (an element may also be 0)
int elapsed_event_num = 0; //number of elapsed events skipped during last refresh
TextLayer **event_text1 = 0; //row one texts
TextLayer **event_text2 = 0; //row two texts
TextLayer **event_time1 = 0; //row one times
TextLayer **event_time2 = 0; //row two times
char **event_texts = 0; //big array for all the texts displayed on the layers. Layout: [row1_time row1_text row2_time row2_text] (repeated for each layer index)

//Font according to settings
GFont font; //font to use for events (and separators)
GFont font_bold; //corresponding bold font for events
int line_height; //will contain height of a line (row) in a event (depends on chosen font height)
int font_index; //contains a two-bit number for the chosen font according to the settings

int num_separators = 0; //number of separators. As many elements will be in the day_separator_layers array
TextLayer **day_separator_layers = 0; //layers for showing weekday
char **day_separator_texts = 0; //texts on separators

Window *window; //the watchface's only window
TextLayer *text_layer_time = 0; //layer for the current time (if header enabled in settings)
TextLayer *text_layer_date = 0; //layer for current date (if header enabled)
TextLayer *text_layer_weekday = 0; //layer for current weekday (if header enabled)
TextLayer *sync_indicator_layer = 0; //sync indicator

GFont time_font = 0; //Font for current time (custom font)
GFont date_font; //Font for the current date (system font)
int time_font_id = -1; //id of time_font according to Android settings (-1 being not loaded)
int header_height = 0; //height of the header
int header_time_width = 0; //width of the time layer
int header_weekday_height = 0; //height of the weekday layer

caltime_t get_current_time() { //shortcut to get current caltime_t
	time_t t = time(NULL);
	return tm_to_caltime(localtime(&t));
}

//Displays progress of synchronization in the layer (if displayed). Setting max == 0 is valid (then no sync bar)
void sync_layer_set_progress(int now, int max) {
	if (sync_indicator_layer == 0)
		return;
	
	int width = max == 0 ? 144 : ((now*144)/max);
	
	layer_set_bounds(text_layer_get_layer(sync_indicator_layer), GRect(width,0,144-width,1));
}

//Set font variables (font, font_bold, line_height) according to settings
void set_font_from_settings() {
	font_index = (int) ((settings_get_bool_flags() & (SETTINGS_BOOL_FONT_SIZE0|SETTINGS_BOOL_FONT_SIZE1))/SETTINGS_BOOL_FONT_SIZE0); //figure out index of the font from settings (two-bit number)
	switch (font_index) {
		case 1:
			font = fonts_get_system_font(FONT_KEY_GOTHIC_18);
			font_bold = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
			line_height = 22;
		break;
		case 2:
			font = fonts_get_system_font(FONT_KEY_GOTHIC_24);
			font_bold = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
			line_height = 28;
		break;
		
		case 0:
		default:
			font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
			font_bold = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
			line_height = 16;
		break;
	}
}

//Calculate from settings how much horizontal space the event time layer should take. I know I could let Pebble measure the text width, but I want this offset to be constant
int get_event_text_offset(uint8_t design_time, uint8_t number_of_times, bool append_am_pm) { //wants a 8 bit number in design_time (cf. call in create_event_layers())
	if (design_time == 0) //no time displayed
		return 0;
	
	int result = font_index == 2 ? 45 : font_index == 1 ? 35 : 28; //start with basic width
	if (append_am_pm) //add some if am/pm is displayed
		result+= font_index == 2 ? 20 : font_index == 1 ? 17 : 15;
	if (number_of_times > 1) { //twice that if actually two times are displayed (like "19:00-20:00")
		result*=2;
	} 
	
	result += font_index == 2 ? 13 : font_index == 1 ? 11 : 9; //add some more
	
	return result;
}

//Create a string from time that can be shown to the user according to settings. relative_to contains the date that the user expects to see (to determine whether to display time or day). If relative_time is true, then the function may print remaining minutes to relative_to.
void time_to_showstring(char* buffer, size_t buffersize, caltime_t time, caltime_t relative_to, bool relative_time, bool hour_12, bool append_am_pm, bool prepend_dash) {
	if (prepend_dash) {
		buffer[0] = '-';
		buffersize--;
		buffer++; //advance pointer by the byte we just added
	}
	
	//Catch times that are not on relative_to, show their date instead
	if (caltime_to_date_only(relative_to) != caltime_to_date_only(time)) { //show weekday instead of time
		static char *daystrings[7] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
		snprintf(buffer, buffersize, "%s", daystrings[caltime_get_weekday(time)]);
	} 
	else if (relative_time && time>=relative_to && time-relative_to <= 60) { //show relative time ("in 5 minutes")
		snprintf(buffer, buffersize, "%dmin", (int) (time-relative_to));
		refresh_at = 1; //force refresh next minute tick
	}
	else { //Show "regular" time
		if (hour_12) {
			int hour = (int) caltime_get_hour(time);
			snprintf(buffer, buffersize, append_am_pm ? (hour < 12 ? "%d:%02dam" : "%d:%02dpm") : "%d:%02d", hour % 12 == 0 ? 12 : hour % 12, (int) caltime_get_minute(time));
		}
		else
			snprintf(buffer, buffersize, append_am_pm ? (caltime_get_hour(time) <= 12 ? "%02ld:%02ldam" : "%02ld:%02ldpm") : "%02ld:%02ld", caltime_get_hour(time), caltime_get_minute(time));
	}
}

//Creates the necessary layers for an event. Returns y+[height that the new layers take]. Every event has up to two rows, both consisting of a time and a text portion (either may be empty)
int create_event_layers(int i, int y, Layer* parent, CalendarEvent* event, caltime_t relative_to, bool relative_time) { //relative_to and relative_time as used in time_to_showstring(...)
	//Get settings
	uint32_t design = settings_get_design();
	uint32_t settings = settings_get_bool_flags();
	bool show_2nd_row = (((settings & SETTINGS_BOOL_SHOW_ROW2) && !event->all_day) || ((settings & SETTINGS_BOOL_AD_SHOW_ROW2) && event->all_day)); //figure out whether this event has a second row
	
	//Design settings offset (all-day vs normal event) - after this, the first 4 bit will contain the design settings appropriate for this event
	if (event->all_day)
		design /= 0x10000;
	
	//Create the row(s)
	for (int row=0; row<2; row++) {
		if (row == 1 && !show_2nd_row) { //if it's the second row's loop turn but we don't display any, set null data
			event_text2[i] = 0;
			event_time2[i] = 0;
			for (int j=0;j<2;j++)
				event_texts[i*4+row*2+j] = 0;
			continue;
		}
		
		//Variables for convenient settings access
		uint8_t design_time = row == 0 ? design%0x10 : (design/0x100)%0x10; //results in two-bit number corresponding to appropriate design for the time (constants as in Android app)
		uint8_t design_text = row == 0 ? (design/0x10)%0x10 : (design/0x1000)%0x10; //results in two-bit number corresponding to appropriate design for the text (constants as in Android app)
		
		//Figure out texts for this row
		if (design_time != 0) { //should we show any time at all?
			event_texts[i*4+row*2] = malloc(20*sizeof(char));
			
			//figure out whether to display start or end time
			caltime_t time_to_show = design_time == 2 ? event->end_time : event->start_time; 
			if (design_time == 4) { //Settings say we should show end_time rather than start time iff event has started
				if (get_current_time() >= event->start_time)
					time_to_show = event->end_time;
			}
			
			time_to_showstring(event_texts[i*4+row*2], 20, time_to_show, relative_to, relative_time, settings & SETTINGS_BOOL_12H ? 1 : 0,(settings & SETTINGS_BOOL_12H) && (settings & SETTINGS_BOOL_AMPM) ? 1 : 0, time_to_show == event->end_time ? 1 : 0);
			if (design_time == 3) //we should show start and end time. So we append the end time
				time_to_showstring(event_texts[i*4+row*2]+strlen(event_texts[i*4+row*2]), 10, event->end_time, relative_to, relative_time && get_current_time() >= event->start_time, settings & SETTINGS_BOOL_12H ? 1 : 0, (settings & SETTINGS_BOOL_12H) && (settings & SETTINGS_BOOL_AMPM) ? 1 : 0, true);
		}
		else
			event_texts[i*4+row*2] = 0;
		
		if (design_text != 0) { //should we show any text at all?
			event_texts[i*4+row*2+1] = malloc(30*sizeof(char));
			strncpy(event_texts[i*4+row*2+1], design_text == 1 ? event->title : event->location , 30); //set text to title or location (depending on settings)
		}
		else
			event_texts[i*4+row*2+1] = 0;
		
		
		//Add the actual layers for this row now
		int x = get_event_text_offset(design_time, design_time==3 ? 2 : 1, (settings & SETTINGS_BOOL_12H) && (settings & SETTINGS_BOOL_AMPM) ? 1 : 0); //desired width of time layer
		
		//Time layer
		TextLayer *layer = text_layer_create(GRect(0,y,x,line_height));
		if (row == 0) //store appropriately depending on which row we're creating right now
			event_time1[i] = layer;
		else
			event_time2[i] = layer;
		text_layer_set_background_color(layer, GColorWhite);
		text_layer_set_text_color(layer, GColorBlack);
		text_layer_set_font(layer, font);
		text_layer_set_text(layer, event_texts[i*4+row*2]);
		layer_add_child(parent, text_layer_get_layer(layer));
		
		//Text layer
		layer = text_layer_create(GRect(x,y,144-x,line_height));
		if (row == 0) //store appropriately depending on which row we're creating right now
			event_text1[i] = layer;
		else
			event_text2[i] = layer;
		text_layer_set_background_color(layer, GColorWhite);
		text_layer_set_text_color(layer, GColorBlack);
		text_layer_set_font(layer, design_text == 1 ? font_bold : font);
		text_layer_set_text(layer, event_texts[i*4+row*2+1]);
		layer_add_child(parent, text_layer_get_layer(layer));
		
		y+=line_height; //add this line's height to y for return value
	}
	
	return y; //screen offset where this event's layers end
}

//Creates separator (like the "Monday" layer, separating today's events from tomorrow's), returns y+[own height]
int create_day_separator_layer(int i, int y, Layer* parent, CalendarEvent* event) {
	static char *daystrings[8] = {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday", "Tomorrow"};
	static char *monthstrings[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Okt", "Nov", "Dez"};
	
	//Set text
	day_separator_texts[i] = malloc(sizeof(char)*20);
	if (settings_get_bool_flags() & SETTINGS_BOOL_SEPARATOR_DATE)
		snprintf(day_separator_texts[i], 20, "%s, %s %02ld", daystrings[cal_begins_tomorrow(event) ? 7 : caltime_get_weekday(event->start_time)], monthstrings[caltime_get_month(event->start_time)-1], caltime_get_day(event->start_time));
	else
		snprintf(day_separator_texts[i], 20, "%s", daystrings[cal_begins_tomorrow(event) ? 7 : caltime_get_weekday(event->start_time)]);
	
	//Create layer
	day_separator_layers[i] = text_layer_create(GRect(0,y,144,line_height));
	text_layer_set_background_color(day_separator_layers[i], GColorBlack);
	text_layer_set_text_color(day_separator_layers[i], GColorWhite);
	text_layer_set_font(day_separator_layers[i], font);
	text_layer_set_text_alignment(day_separator_layers[i], GTextAlignmentRight);
	text_layer_set_text(day_separator_layers[i], day_separator_texts[i]);
	layer_add_child(parent, text_layer_get_layer(day_separator_layers[i]));
	
	return y+line_height;
}

bool should_be_displayed(CalendarEvent* event) { //predicate for a calendar event. true iff event has not ended yet
	return event->end_time >= get_current_time();
}

void display_cal_data() { //(Re-)creates all the layers for the events in the database and shows them. (Re-)creates event_layer_... arrays
	Layer *window_layer = window_get_root_layer(window);
	if (event_db_size() <= 0)
		return;
	
	//Create arrays
	event_text1 = malloc(sizeof(TextLayer*)*event_db_size());
	event_text2 = malloc(sizeof(TextLayer*)*event_db_size());
	event_time1 = malloc(sizeof(TextLayer*)*event_db_size());
	event_time2 = malloc(sizeof(TextLayer*)*event_db_size());
	event_texts = malloc(sizeof(char*)*event_db_size()*4); //a string for every text layer
	day_separator_layers = malloc(sizeof(TextLayer*)*event_db_size());
	day_separator_texts = malloc(sizeof(char*)*event_db_size());
	
	//Figure out font to use
	set_font_from_settings();
	
	//Iterate over calendar events
	num_events = 0;
	elapsed_event_num = 0;
	num_separators = 0;
	refresh_at = 0; //contains the earliest time that we need to schedule a refresh for
	CalendarEvent *previous_event = 0; //contains the event from previous loop iteration (or 0)
	int y = header_height; //vertical offset to start displaying layers
	caltime_t now = get_current_time();
	caltime_t last_separator_date = now; //the date (time portion should be ignored) of the last day separator (so that times can be shown relative to that)

	for (int i=0;i<event_db_size()&&y<168;i++) {
		CalendarEvent* event = event_db_get(i);
		if (!should_be_displayed(event)) { //skip those that we shouldn't display
			elapsed_event_num++;
			continue;
		}
				
		//Check if we need a date separator
		if ((previous_event == 0 && !cal_begins_before_tomorrow(event)) || (previous_event != 0 && cal_begins_later_day(previous_event, event) && !cal_begins_before_tomorrow(event))) {
			y = create_day_separator_layer(num_separators, y, window_layer, event);
			last_separator_date = event->start_time;
			num_separators++;
		}
		
		//Add event layers
		y = create_event_layers(num_events, y, window_layer, event, last_separator_date, num_separators == 0 && (settings_get_bool_flags() & SETTINGS_BOOL_COUNTDOWNS))+1;
		num_events++;
		
		//Schedule refresh for when the event starts or ends
		if ((refresh_at == 0 || refresh_at > event->start_time) && event->start_time > now)
			refresh_at = event->start_time;
		if (refresh_at == 0 || refresh_at > event->end_time)
			refresh_at = event->end_time;
		
		previous_event = event;
	}
	
	//Adjust refresh_at for countdown functionality. The other adjustment (for when a countdown is currently active) happens in the time_to_showstring() function
	if ((settings_get_bool_flags() & SETTINGS_BOOL_COUNTDOWNS) && refresh_at != 0 && refresh_at % 60*60 >=60)
		refresh_at -= 60; //so that we can begin the countdown there
}

void remove_cal_data() { //tidies up anything in the event_layer_... arrays
	for (int i=0;i<num_events;i++) {
		if (event_text1[i] != 0)
			text_layer_destroy(event_text1[i]);
		if (event_text2[i] != 0)
			text_layer_destroy(event_text2[i]);
		if (event_time1[i] != 0)
			text_layer_destroy(event_time1[i]);
		if (event_time2[i] != 0)
			text_layer_destroy(event_time2[i]);
		for (int j=0; j<4; j++) {
			if (event_texts[i*4+j] != 0)
				free(event_texts[i*4+j]);
		}
	}
	for (int i=0;i<num_separators;i++) {
		text_layer_destroy(day_separator_layers[i]);
		free(day_separator_texts[i]);
	}
	
	num_events = 0;
	num_separators = 0;
	if (event_text1 != 0)
		free(event_text1);
	if (event_text2 != 0)
		free(event_text2);
	if (event_time1 != 0)
		free(event_time1);
	if (event_time2 != 0)
		free(event_time2);
	if (day_separator_layers != 0)
		free(day_separator_layers);
	if (event_texts != 0)
		free(event_texts);
	if (day_separator_texts != 0)
		free(day_separator_texts);
	
	event_text1 = 0;
	event_text2 = 0;
	event_time1 = 0;
	event_time2 = 0;
	event_texts = 0;
	day_separator_layers = 0;
	day_separator_texts = 0;
}

void handle_no_new_data() { //sync done, no new data
	last_sync = time(NULL);
}

void handle_new_data(uint8_t sync_id) { //Sync done. Show new data from database
	display_cal_data(); //Create the event layers etc.
	
	last_sync = time(NULL); //remember successful sync
	last_sync_id = sync_id;
	persist_write_data(PERSIST_LAST_SYNC_ID, &last_sync_id, sizeof(last_sync_id));
	
	event_db_persist(); //save database persistently
}

void handle_data_gone() { //Database will go down. Stop showing stuff
	remove_cal_data();
}

void update_clock() { //updates the layer for the current time (if exists)
	if (text_layer_time == 0)
		return;
	static char time_text[] = "00:00";
	clock_copy_time_string(time_text, sizeof(time_text));
	text_layer_set_text(text_layer_time,  time_text);
}

void update_date(struct tm *time) { //updates the layer for the current date (if exists)
	if (text_layer_date != 0) {
		static char date_text[] = "NameOfTheMonth 01";
		
		if (header_time_width <= 75) //if time takes much vertical space, abbreviate
			strftime(date_text, sizeof(date_text), "%B %d", time); //don't abbreviate
		else
			strftime(date_text, sizeof(date_text), "%b %d", time); //abbreviate
		
		date_text[sizeof(date_text)-1] = 0;
		text_layer_set_text(text_layer_date, date_text);
	}
	
	if (text_layer_weekday != 0) {
		static char weekday_text[] = "Wednesday";
		
		if (header_time_width <= 75) //if time takes much vertical space, abbreviate
			strftime(weekday_text, sizeof(weekday_text), "%A", time); //don't abbreviate
		else
			strftime(weekday_text, sizeof(weekday_text), "%a", time); //abbreviate
		
		weekday_text[sizeof(weekday_text)-1] = 0;
		text_layer_set_text(text_layer_weekday, weekday_text);
	}
}

static void handle_time_tick(struct tm *tick_time, TimeUnits units_changed) { //handle OS call for ticking time (every minute)
	//Update clock value
	update_clock();
	
	//Update date value if changed
	if (units_changed & DAY_UNIT)
		update_date(tick_time);
	
	//check whether we should try for an update (if connected and last sync was more than (30-10*elapsed_event_num) minutes ago). Also when time went backward (time zoning)
	if (bluetooth_connection_service_peek() && (time(NULL)-last_sync > 60*30-60*15*elapsed_event_num || time(NULL) < last_sync))
		send_sync_request(last_sync_id);
	
	//check whether we crossed the refresh_at threshold (e.g., event finished and has to be removed. Or event starts and now has to show endtime...)
	if ((tick_time->tm_hour == 0 && tick_time->tm_min == 1) || (refresh_at != 0 && tm_to_caltime(tick_time) >= refresh_at)) {
		APP_LOG(APP_LOG_LEVEL_DEBUG, "Refreshing currently shown events");
		//Reset what's displayed and redisplay
		remove_cal_data();
		display_cal_data();
	}
}

//Populates time_font, time_font_id and header_height
void set_time_font_from_settings() {
	int time_font_id_new = (int) ((settings_get_bool_flags() & (SETTINGS_BOOL_HEADER_SIZE0|SETTINGS_BOOL_HEADER_SIZE1))/SETTINGS_BOOL_HEADER_SIZE0); //figure out index of the font from settings (two-bit number)
	
	if (time_font_id_new != time_font_id) {
		//Unload previous font
		if (time_font != 0)
			fonts_unload_custom_font(time_font);
		
		//Apply new font
		time_font_id = time_font_id_new;
		switch (time_font_id) {
			case 1: //big time
				time_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ROBOTO_CONDENSED_BOLD_38));
				break;
			case 0:
			default:
				time_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ROBOTO_CONDENSED_30));
				break;
		}
	}
	
	//Apply other settings
	switch (time_font_id) {
		case 1: //big time/header
			date_font = fonts_get_system_font(FONT_KEY_GOTHIC_18);
			header_weekday_height = 18;
			header_height = 48;
			header_time_width = 95;
		break;
		
		case 0: //small time/header
		default:
			date_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
			header_weekday_height = 16;
			header_height = 40;
			header_time_width = 75;
		break;
	}
}

//Create the header that shows current time and date (if settings say so)
void create_header(Layer *window_layer) {
	if (!(settings_get_bool_flags() & SETTINGS_BOOL_SHOW_CLOCK_HEADER)) { //stop creating here if user settings permit
		header_height = 2;
	}
	else {
		//Figure out font for the time
		set_time_font_from_settings(); //also sets header_height etc.
		
		//Create time layer
		text_layer_time = text_layer_create(GRect(0, 0, header_time_width, header_height));
		text_layer_set_background_color(text_layer_time, GColorBlack);
		text_layer_set_text_color(text_layer_time, GColorWhite);
		text_layer_set_font(text_layer_time, time_font);
		layer_add_child(window_layer, text_layer_get_layer(text_layer_time));
		
		//Create date layer
		text_layer_date = text_layer_create(GRect(header_time_width, header_weekday_height, 144-header_time_width, header_height-header_weekday_height));
		text_layer_set_background_color(text_layer_date, GColorBlack);
		text_layer_set_text_color(text_layer_date, GColorWhite);
		text_layer_set_text_alignment(text_layer_date, GTextAlignmentRight);
		text_layer_set_font(text_layer_date, date_font);
		layer_add_child(window_layer, text_layer_get_layer(text_layer_date));
		
		//Create weekday layer
		text_layer_weekday = text_layer_create(GRect(header_time_width, 0, 144-header_time_width, header_weekday_height));
		text_layer_set_background_color(text_layer_weekday, GColorBlack);
		text_layer_set_text_color(text_layer_weekday, GColorWhite);
		text_layer_set_text_alignment(text_layer_weekday, GTextAlignmentRight);
		text_layer_set_font(text_layer_weekday, date_font);
		layer_add_child(window_layer, text_layer_get_layer(text_layer_weekday));
		
		//Show initial values
		update_clock();
		time_t t = time(NULL);
		update_date(localtime(&t));
	}
	
	//Create sync indicator
	sync_indicator_layer = text_layer_create(GRect(0,0,144,1));
	text_layer_set_background_color(sync_indicator_layer, GColorWhite);
	layer_add_child(window_layer, text_layer_get_layer(sync_indicator_layer));
	layer_add_child(window_layer, text_layer_get_layer(sync_indicator_layer));
	layer_set_bounds(text_layer_get_layer(sync_indicator_layer), GRect(0,0,0,0)); //relative to own frame
}

//Well... Destroys whatever create_header() created...
void destroy_header() {
	if (text_layer_time != 0) text_layer_destroy(text_layer_time);
	if (text_layer_date != 0) text_layer_destroy(text_layer_date);
	if (text_layer_weekday != 0) text_layer_destroy(text_layer_weekday);
	if (sync_indicator_layer != 0) text_layer_destroy(sync_indicator_layer);
	
	text_layer_time = 0;
	text_layer_date = 0;
	text_layer_weekday = 0;	
	sync_indicator_layer = 0;
}

//Callback if settings changed (also called in handle_init()). We'll simply destroy everything, recreate the header if still set to. Calendar data will be shown again after sync is done
void handle_new_settings() {
	remove_cal_data();
	destroy_header();
	create_header(window_get_root_layer(window));
}

//Create all necessary structures, etc.
void handle_init(void) {
	//Init window
	window = window_create();
	window_stack_push(window, true);
 	window_set_background_color(window, GColorBlack);
	
	//Read persistent data
	if (persist_exists(PERSIST_LAST_SYNC)) {
		persist_read_data(PERSIST_LAST_SYNC, &last_sync, sizeof(last_sync));
		if (time(NULL)-last_sync > 60*15) //force sync more often (not too often) if the watchface was left in the meantime...
			last_sync = 0;
	}
	else
		last_sync = 0;
	
	if (persist_exists(PERSIST_LAST_SYNC_ID)) {
		persist_read_data(PERSIST_LAST_SYNC_ID, &last_sync_id, sizeof(last_sync_id));
	}
	else
		last_sync_id = 0;
	
	event_db_restore_persisted();
	settings_restore_persisted();
	
	//Create some initial stuff depending on settings
	handle_new_settings();
	
	//Show data from database
	display_cal_data();	
	
	//Register services
	tick_timer_service_subscribe(MINUTE_UNIT, &handle_time_tick);
	
	//Register for communication events
	app_message_register_inbox_received(in_received_handler);	
	app_message_register_inbox_dropped(in_dropped_handler);
	app_message_register_outbox_sent(out_sent_handler);
	app_message_register_outbox_failed(out_failed_handler);
	
	//Begin listening to messages
	const uint32_t inbound_size = 124; //should be the max value
	const uint32_t outbound_size = 64; //we don't send much
	app_message_open(inbound_size, outbound_size);
}

//Destroy what handle_init() created
void handle_deinit(void) {
	//Unsubscribe callbacks
	accel_tap_service_unsubscribe();
	tick_timer_service_unsubscribe();
	app_message_deregister_callbacks();
	
	//Destroy ui
	destroy_header();
	remove_cal_data();
	window_destroy(window);
	
	//Unload font(s)
	if (time_font != 0)
		fonts_unload_custom_font(time_font);
	
	//Write persistent data
	persist_write_data(PERSIST_LAST_SYNC, &last_sync, sizeof(last_sync));
	//event_db_persist(); //is persisted when new data arrives so this doesn't take so long here
	//settings_persist(); //is persisted when new settings arrive
	
	//Destroy last references
	event_db_reset();
	communication_cleanup();
}

int main(void) {
	  handle_init();
	  app_event_loop();
	  handle_deinit();
}
