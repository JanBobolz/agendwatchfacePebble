#include <pebble.h>
#include <item_db.h>
#include <datatypes.h>
#include <communication.h>
#include <settings.h>
#include <persist_const.h>
#include <main.h>
	
time_t last_sync = 0; //time where the last successful sync happened
uint8_t last_sync_id = 0; //id that the phone supplied for the last successful sync
caltime_t refresh_at = 0; //time where the item display should be refreshed next

int num_layers = 0; //number of elements in item_layer and item_text
int elapsed_item_num = 0; //number of items skipped because they were elapsed
TextLayer **item_layers = 0; //list of all layers that were created for the displayed items
char **item_texts = 0; //list of texts. item_text[i] corresponds to item_layer[i]. Does not necessarily include all strings (e.g., event texts are saved in the event)

//Font according to settings
GFont font; //font to use for items (and separators)
GFont font_bold; //corresponding bold font
int line_height = 0; //will contain height of a line (row) in an item (depends on chosen font height)
int font_index; //contains a two-bit number for the chosen font according to the settings

int num_separators = 0; //number of separators. As many elements will be in the day_separator_layers array
TextLayer **day_separator_layers = 0; //layers for showing weekday
char **day_separator_texts = 0; //texts on separators

Window *window; //the watchface's only window
Layer *root_layer; //the layer containing the window's content (different from window_get_root_layer(window))
InverterLayer *inverter_layer = 0; //the inverter layer or 0 if not applicable
TextLayer *text_layer_time = 0; //layer for the current time (if header enabled in settings)
TextLayer *text_layer_date = 0; //layer for current date (if header enabled)
TextLayer *text_layer_weekday = 0; //layer for current weekday (if header enabled)
TextLayer *sync_indicator_layer = 0; //sync indicator

PropertyAnimation *scroll_animation = 0; //current scrolling animation or 0
AppTimer* scroll_reset_timer = 0; //timer handle to reset scroll position after some time
int scroll_position = 0; //current y-axis scrolling position (or the one that's being scrolled to)
int items_biggest_y = 0; //the y position of the last displayed item

Animation* continuous_scroll_anim = 0; //current continuous scroll animation or 0
time_t anim_last_milestone_s = 0; //time in seconds of the last milestone (i.e. the last time, scroll speed has been reset)
uint16_t anim_last_milestone_ms = 0; //the milisecond part of anim_last_milestone_s
int anim_last_milestone_y = 0; //the scroll_position at the previous milestone
int16_t anim_scroll_speed = 0; //the speed of scrolling (positive: down) in pixels per second
uint8_t anim_num_milestones = 0; //number of passed milestones in the current scrolling process (to make deactivation harder at first)

GFont time_font = 0; //Font for current time (custom font)
GFont date_font; //Font for the current date (system font)
int time_font_id = -1; //id of time_font according to Android settings (-1 being not loaded)
int header_height = 0; //height of the header
int header_time_y_offset = 0; //vertical offset of the time in the header
int header_weekday_y_offset = 0; //vertical offset of the right side of the header
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

//Calculate from settings how much horizontal space the time layer should take. I know I could let Pebble measure the text width, but I want this offset to be constant for a consistent look
int get_item_text_offset(uint8_t row_design, uint8_t number_of_times, bool append_am_pm) { //wants the row design
	if ((row_design/ROW_DESIGN_TIME_TYPE_OFFSET)%0x8 == 0) //no time displayed
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

void set_refresh_at_if_decrease(caltime_t t) { //convenience function
	if (refresh_at == 0 || t < refresh_at)
		refresh_at = t;
}

//Create a string from time that can be shown to the user according to settings. relative_to contains the date that the user expects to see (to determine whether to display time or day). If relative_time is true, then the function may print remaining minutes to relative_to.
void time_to_showstring(char* buffer, size_t buffersize, caltime_t time, caltime_t relative_to, bool relative_time, bool hour_12, bool append_am_pm, bool prepend_dash) {
	if (prepend_dash) {
		buffer[0] = '-';
		buffersize--;
		buffer++; //advance pointer by the byte we just added
	}
	
	//Catch times that are not on relative_to (and not on the day after, but early in the night), show their date instead
	if (caltime_to_date_only(relative_to) != caltime_to_date_only(time) && !(caltime_get_tomorrow(relative_to) == caltime_to_date_only(time) && caltime_get_hour(time) < 3)) { //show weekday instead of time
		static char *daystrings[7] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
		snprintf(buffer, buffersize, "%s", daystrings[caltime_get_weekday(time)]);
	}
	else if (relative_time && time>=relative_to && time-relative_to <= 60) { //show relative time ("in 5 minutes"). Condition implies that they're on the same day
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
		set_refresh_at_if_decrease(time+(relative_time && time%(24*60*60) >= 60 ? -60 : 0)); //refresh at "time" (if countdown active, refresh already 60 minutes before that)
	}
}

//Creates the necessary layers for an item. Returns y+[height that the new layers take]. Every item has up to two rows, both consisting of a time and a text portion (either may be empty)
int create_item_layers(int y, Layer* parent, AgendaItem* item, caltime_t relative_to, bool relative_time) { //relative_to and relative_time as used in time_to_showstring(...)
	//Get settings
	uint32_t settings = settings_get_bool_flags();
	
	//Create the row(s)
	for (int row=0; row<2; row++) {
		if (row == 1 && item->row2design == 0) //skip second row if design says so
			continue;
		
		//Convenience variables
		uint8_t row_design = row == 0 ? item->row1design : item->row2design;
		uint8_t design_time = (row_design/ROW_DESIGN_TIME_TYPE_OFFSET)%0x8;
		uint8_t row_overflow = (row_design/ROW_DESIGN_TEXT_OVERFLOW_OFFSET)%0x4;
		char* row_text = row == 0 ? item->row1text : item->row2text;
		
		//Figure out height of this line and the width of the time
		int time_layer_width = get_item_text_offset(row_design, design_time==3 ? 2 : 1, (settings & SETTINGS_BOOL_12H) && (settings & SETTINGS_BOOL_AMPM) ? 1 : 0); //desired width of time layer
		int line_height_factor = row_overflow == 2 ? 2 : 1;
		if (row_overflow == 1 && graphics_text_layout_get_content_size(row_text, row_design & ROW_DESIGN_TEXT_BOLD ? font_bold : font, GRect(time_layer_width,y,144-time_layer_width,line_height*2), GTextOverflowModeFill, GTextAlignmentLeft).h > line_height)  //row_overflow == 1: overflow if necessary
			line_height_factor = 2;
		
		//Create time text and layer
		if (design_time != 0) { //should we show any time at all?
			item_texts[num_layers] = malloc(20*sizeof(char));
			//figure out whether to display start or end time
			caltime_t time_to_show = design_time == 2 ? item->end_time : item->start_time; 
			if (design_time == 4) { //Settings say we should show end_time rather than start time iff item has started
				if (get_current_time() >= item->start_time)
					time_to_show = item->end_time;
			}
			
			time_to_showstring(item_texts[num_layers], 20, time_to_show, relative_to, relative_time, settings & SETTINGS_BOOL_12H ? 1 : 0,(settings & SETTINGS_BOOL_12H) && (settings & SETTINGS_BOOL_AMPM) ? 1 : 0, time_to_show == item->end_time ? 1 : 0);
			if (design_time == 3) //we should show start and end time. So we append the end time
				time_to_showstring(item_texts[num_layers]+strlen(item_texts[num_layers]), 10, item->end_time, relative_to, relative_time && get_current_time() >= item->start_time, settings & SETTINGS_BOOL_12H ? 1 : 0, (settings & SETTINGS_BOOL_12H) && (settings & SETTINGS_BOOL_AMPM) ? 1 : 0, true);
		
			//Create time layer
			TextLayer *layer = text_layer_create(GRect(0,y,time_layer_width,line_height*line_height_factor));
			text_layer_set_background_color(layer, GColorWhite);
			text_layer_set_text_color(layer, GColorBlack);
			text_layer_set_font(layer, font);
			text_layer_set_text(layer, item_texts[num_layers]);
			layer_add_child(parent, text_layer_get_layer(layer));
			item_layers[num_layers++] = layer;
		}
		
		//Create text layer
		char* text = 0;
		if (row_text != 0)
			text = row_text; //set the reference to the text saved in the event struct
		item_texts[num_layers] = 0; //no reference in item_texts for this layer (as the text should not be freed when tidying up UI, only by the database)
		
		TextLayer *layer = text_layer_create(GRect(time_layer_width,y,144-time_layer_width,line_height*line_height_factor));
		text_layer_set_background_color(layer, GColorWhite);
		text_layer_set_text_color(layer, GColorBlack);
		text_layer_set_font(layer, row_design & ROW_DESIGN_TEXT_BOLD ? font_bold : font);
		text_layer_set_overflow_mode(layer, GTextOverflowModeFill);
		if (text != 0)
			text_layer_set_text(layer, text);
		layer_add_child(parent, text_layer_get_layer(layer));
		item_layers[num_layers++] = layer;
		
		y+=line_height*line_height_factor; //add this line's height to y for return value
	}
	
	return y; //screen offset where this item's layers end
}

//Creates separator (like the "Monday" layer, separating today's items from tomorrow's), returns y+[own height]
int create_day_separator_layer(int i, int y, Layer* parent, caltime_t day) {
	static char *daystrings[8] = {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday", "Tomorrow"};
	static char *monthstrings[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Okt", "Nov", "Dez"};
	
	//Set text
	day_separator_texts[i] = malloc(sizeof(char)*20);
	if (settings_get_bool_flags() & SETTINGS_BOOL_SEPARATOR_DATE)
		snprintf(day_separator_texts[i], 20, "%s, %s %02ld", daystrings[caltime_to_date_only(day) == caltime_get_tomorrow(get_current_time()) ? 7 : caltime_get_weekday(day)], monthstrings[caltime_get_month(day)-1], caltime_get_day(day));
	else
		snprintf(day_separator_texts[i], 20, "%s", daystrings[caltime_to_date_only(day) == caltime_get_tomorrow(get_current_time()) ? 7 : caltime_get_weekday(day)]);
	
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

void display_data() { //(Re-)creates all the layers for items in the database and shows them. (Re-)creates item_layers, item_texts, ... arrays
	Layer *window_layer = root_layer;
	if (db_size() <= 0)
		return;
	
	//Create arrays
	item_layers = malloc(sizeof(TextLayer*)*db_size()*4); //maximal four layers per item in the db
	item_texts = malloc(sizeof(char*)*db_size()*4);
	day_separator_layers = malloc(sizeof(TextLayer*)*db_size()); //maximal one day-separator per item
	day_separator_texts = malloc(sizeof(char*)*db_size());
	
	//Figure out font to use
	set_font_from_settings();
	
	//Iterate over agenda items
	num_layers = 0;
	elapsed_item_num = 0;
	num_separators = 0;
	refresh_at = 0; //contains the earliest time that we need to schedule a refresh for
	AgendaItem *previous_item = 0; //contains the item from previous loop iteration (or 0)
	int y = header_height; //vertical offset to start displaying layers
	caltime_t now = get_current_time();
	caltime_t last_separator_date = now; //the date of the last day separator (so that times can be shown relative to that)
	caltime_t tomorrow_date = caltime_get_tomorrow(now);

	for (int i=0;i<db_size();i++) {
		AgendaItem* item = db_get(i);
		if (item->end_time != 0 && item->end_time < now) { //skip those that we shouldn't display
			elapsed_item_num++;
			continue;
		}
				
		//Check if we need a date separator
		if ((previous_item == 0 && caltime_to_date_only(item->start_time) >= tomorrow_date) //first item doesn't begin before tomorrow
			|| (previous_item != 0 && caltime_to_date_only(previous_item->start_time) != caltime_to_date_only(item->start_time) && caltime_to_date_only(item->start_time) >= tomorrow_date)) { //it's not the first item, but the previous one belonged to another date and this one doesn't start until tomorrow
			y = create_day_separator_layer(num_separators, y, window_layer, item->start_time);
			last_separator_date = item->start_time;
			num_separators++;
		}
		
		//Add item layers
		y = create_item_layers(y, window_layer, item, last_separator_date, (settings_get_bool_flags() & SETTINGS_BOOL_COUNTDOWNS) && num_separators == 0)+1;
		
		//refresh_at is set by time_to_showstring() for shown times. Make sure that items disappear after their expiration even when not showing the time
		if (item->end_time != 0)
			set_refresh_at_if_decrease(item->end_time);
		
		previous_item = item;
	}
	
	items_biggest_y = y;
}

void remove_displayed_data() { //tidies up anything that display_data() created
	for (int i=0;i<num_layers;i++) {
		if (item_layers[i] != 0)
			text_layer_destroy(item_layers[i]);
		if (item_texts[i] != 0)
			free(item_texts[i]);
	}
	for (int i=0;i<num_separators;i++) {
		text_layer_destroy(day_separator_layers[i]);
		free(day_separator_texts[i]);
	}
	
	num_layers = 0;
	num_separators = 0;
	if (item_layers != 0)
		free(item_layers);
	if (item_texts != 0)
		free(item_texts);
	if (day_separator_layers != 0)
		free(day_separator_layers);
	if (day_separator_texts != 0)
		free(day_separator_texts);
	
	item_layers = 0;
	item_texts = 0;
	day_separator_layers = 0;
	day_separator_texts = 0;
}

void handle_no_new_data() { //sync done, no new data
	last_sync = time(NULL);
}

void handle_new_data(uint8_t sync_id) { //Sync done. Show new data from database
	display_data(); //Create the item layers etc.
	
	last_sync = time(NULL); //remember successful sync
	last_sync_id = sync_id;
	
	//scroll(0);
}

void handle_sync_failed() {
	last_sync = 0;
	send_sync_request(last_sync_id);
}

void handle_data_gone() { //Database will go down. Stop showing stuff, as the texts are gone
	remove_displayed_data();
}

void update_clock() { //updates the layer for the current time (if exists)
	if (text_layer_time == 0)
		return;
	static char time_text[] = "00:00";
	clock_copy_time_string(time_text, sizeof(time_text));
	text_layer_set_text(text_layer_time, time_text);
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
		uint8_t battery_percent = battery_state_service_peek().charge_percent;
		
		if (battery_percent <= 20 || battery_state_service_peek().is_charging) {
			snprintf(weekday_text, sizeof(weekday_text), header_time_width <= 75 ? "Bat: %d %%" : "B: %d%%", battery_percent);
		}
		else {
			if (header_time_width <= 75) //if time takes much vertical space, abbreviate
				strftime(weekday_text, sizeof(weekday_text), "%A", time); //don't abbreviate
			else
				strftime(weekday_text, sizeof(weekday_text), "%a", time); //abbreviate
		}
		
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
	
	//check whether we should try for an update (if connected and last sync was more than (30-20*elapsed_item_num) minutes ago). Also when time went backward (time zoning/DST)
	if (time(NULL)-last_sync > 60*30-60*20*elapsed_item_num || time(NULL) < last_sync)
		send_sync_request(last_sync_id);
	
	//APP_LOG(APP_LOG_LEVEL_DEBUG, "refresh_at = %ld (h:%ld m:%ld)", refresh_at, caltime_get_hour(refresh_at), caltime_get_minute(refresh_at));
	//check whether we crossed the refresh_at threshold (e.g., item finished and has to be removed. Or item starts and now has to show endtime...)
	if ((tick_time->tm_hour == 0 && tick_time->tm_min == 0) || (refresh_at != 0 && tm_to_caltime(tick_time) > refresh_at)) {
		APP_LOG(APP_LOG_LEVEL_DEBUG, "Refreshing currently shown items");
		//Reset what's displayed and redisplay
		remove_displayed_data();
		display_data();
	}
}

static void handle_battery(BatteryChargeState charge_state) {
	time_t now = time(NULL);
	update_date(localtime(&now));
}

void bluetooth_connection_callback(bool connected) {
	if (connected)
		last_sync = 0; //force sync
	sync_layer_set_progress(0, connected ? 0 : 1);	
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
				time_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ROBOTO_CONDENSED_BOLD_40));
				break;
			case 0:
			default:
				time_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ROBOTO_CONDENSED_32));
				break;
		}
	}
	
	//Apply other settings
	switch (time_font_id) {
		case 1: //big time/header
			date_font = fonts_get_system_font(FONT_KEY_GOTHIC_18);
			header_weekday_height = 18;
			header_height = 44;
			header_time_width = 98;
			header_time_y_offset = -6;
			header_weekday_y_offset = -2;
		break;
		
		case 0: //small time/header
		default:
			date_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
			header_weekday_height = 14;
			header_height = 32;
			header_time_width = 77;
			header_time_y_offset = -6;
			header_weekday_y_offset = -2;
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
		text_layer_time = text_layer_create(GRect(0, header_time_y_offset, header_time_width, header_height));
		text_layer_set_background_color(text_layer_time, GColorBlack);
		text_layer_set_text_color(text_layer_time, GColorWhite);
		text_layer_set_font(text_layer_time, time_font);
		text_layer_set_overflow_mode(text_layer_time, GTextOverflowModeWordWrap);
		layer_add_child(window_layer, text_layer_get_layer(text_layer_time));
		
		//Create date layer
		text_layer_date = text_layer_create(GRect(header_time_width, header_weekday_y_offset+header_weekday_height, 144-header_time_width, header_height-header_weekday_height));
		text_layer_set_background_color(text_layer_date, GColorBlack);
		text_layer_set_text_color(text_layer_date, GColorWhite);
		text_layer_set_text_alignment(text_layer_date, GTextAlignmentRight);
		text_layer_set_overflow_mode(text_layer_date, GTextOverflowModeWordWrap);
		text_layer_set_font(text_layer_date, date_font);
		layer_add_child(window_layer, text_layer_get_layer(text_layer_date));
		
		//Create weekday layer
		text_layer_weekday = text_layer_create(GRect(header_time_width, header_weekday_y_offset, 144-header_time_width, header_weekday_height));
		text_layer_set_background_color(text_layer_weekday, GColorBlack);
		text_layer_set_text_color(text_layer_weekday, GColorWhite);
		text_layer_set_text_alignment(text_layer_weekday, GTextAlignmentRight);
		text_layer_set_overflow_mode(text_layer_weekday, GTextOverflowModeWordWrap);
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

//Scrolls back to 0
void scroll_reset_timer_callback(void* data) {
	scroll(0);
	scroll_reset_timer = 0;
}

//Reacts to tap event by scrolling and preparing to reset the scrolling position
void accel_tap_handler(AccelAxisType axis, int32_t direction) {
	if (settings_get_bool_flags() & SETTINGS_BOOL_ENABLED_ALT_SCROLL) {
		if (scroll_reset_timer != 0) {
			app_timer_cancel(scroll_reset_timer);
			scroll_reset_timer = 0;
		}
		scroll_reset_timer = app_timer_register(30000, scroll_reset_timer_callback, NULL); //set timer to reset scroll position to 0
		start_scroll_continuously();
	} else if (!(settings_get_bool_flags() & SETTINGS_BOOL_ENABLED_ALT_SCROLL) && scroll_animation == 0) {
		int scroll_amount = line_height == 0 ? 130 : 168-3*line_height; //scroll about half the visible lines away
		scroll(scroll_position+168 > items_biggest_y ? 0 : scroll_position+scroll_amount+168+10 > items_biggest_y ? items_biggest_y-168+1 : scroll_position+scroll_amount); //the +10 are to make these "tiny-step" scrollings to the end rarer
		
		if (scroll_reset_timer != 0) {
			app_timer_cancel(scroll_reset_timer);
			scroll_reset_timer = 0;
		}
		if (scroll_position != 0) //set by scroll()
			scroll_reset_timer = app_timer_register(7000, scroll_reset_timer_callback, NULL); //set timer to reset scroll position to 0
	}
	if (settings_get_bool_flags() & SETTINGS_BOOL_LIGHT_WHILE_SCROLLING)
		light_enable_interaction();
}

//Callback if settings changed (also called in handle_init()). We'll simply destroy everything, recreate the header if still set to. Calendar data will be shown again after sync is done
void handle_new_settings() {
	remove_displayed_data();
	destroy_header();
	create_header(root_layer);
	
	accel_tap_service_unsubscribe();
	
	if (settings_get_bool_flags() & SETTINGS_BOOL_ENABLE_SCROLL)
		accel_tap_service_subscribe(&accel_tap_handler);
	
	if (settings_get_bool_flags() & SETTINGS_BOOL_INVERT) {
		if (inverter_layer == 0) {
			inverter_layer = inverter_layer_create(GRect(0,0,144,168));
			layer_add_child(window_get_root_layer(window), inverter_layer_get_layer(inverter_layer));
		}
	} else {
		if (inverter_layer != 0) {
			inverter_layer_destroy(inverter_layer);
			inverter_layer = 0;
		}
	}
}

//Destroys current animation (not sure if this would be safe to call during a running animation)
void scroll_cleanup() {
	if (scroll_animation != 0) {
		animation_unschedule((struct Animation*) scroll_animation);
		property_animation_destroy(scroll_animation);
	}
	scroll_animation = 0;
}

void scroll_animation_started(Animation *animation, void *data) {
   
}

void scroll_animation_stopped(Animation *animation, bool finished, void *data) {
	scroll_cleanup();
}

void scroll(int y) { //scolls so that the top of the window is offset by y (also ends continuous scrolling if active)
	if (scroll_animation != 0) //animation still running
		return;
	if (continuous_scroll_anim != 0) //stop continuous scrolling
		continuous_scroll_cleanup();
	
	GRect from_frame = layer_get_frame(root_layer);
    GRect to_frame = GRect(0, -y, from_frame.size.w, from_frame.size.h);

    scroll_animation = property_animation_create_layer_frame(root_layer, &from_frame, &to_frame);
	animation_set_delay((struct Animation*) scroll_animation, 100);
	animation_set_handlers((struct Animation*) scroll_animation, (AnimationHandlers) {
        .started = (AnimationStartedHandler) scroll_animation_started,
        .stopped = (AnimationStoppedHandler) scroll_animation_stopped,
      }, NULL);
    animation_schedule((Animation*) scroll_animation);
	scroll_position = y;
}

//Stops continuous scrolling and cleans up everything (safe to call at any point in time)
void continuous_scroll_cleanup() {
	if (continuous_scroll_anim != 0) {
		animation_unschedule(continuous_scroll_anim);
		animation_destroy(continuous_scroll_anim);
	}
	continuous_scroll_anim = 0;
	accel_data_service_unsubscribe();
}

//Implements the continuous scrolling behavior. This function is automatically called from time to time while the animation runs. From time to time, it sets a milestone, reading the acceloremeter to check for new scrolling direction. This function also ends the scrolling process when appropriate
void continuous_animation_impl(struct Animation *animation, const uint32_t time_normalized) {
	static time_t now_s = 0;
	static uint16_t now_ms = 0;
	time_ms(&now_s, &now_ms);
	
	//How much time (in ms) has passed since last milestone?
	int time_delta = (now_s-anim_last_milestone_s)*1000+(now_ms-anim_last_milestone_ms);

	//Move by that much
	if (anim_scroll_speed != 0) {
		scroll_position = anim_last_milestone_y+time_delta*anim_scroll_speed/1000;
		if (scroll_position < 0) { //end scrolling when we're at the top again
			scroll_position = 0;
			if (anim_num_milestones > 10) //be lenient with deactivation at first
				continuous_scroll_cleanup();
		} else if (scroll_position > items_biggest_y-168+1) {
			scroll_position = items_biggest_y-168+1;
		}
		layer_set_frame(root_layer, GRect(0,-scroll_position,144,168));
	}
	
	//Is it time for a new milestone yet?
	if (time_delta > 100) {
		if (anim_num_milestones < 255) //prevent overflow
			anim_num_milestones++;
		anim_last_milestone_y = scroll_position;
		anim_last_milestone_s = now_s;
		anim_last_milestone_ms = now_ms;
		AccelData data;
		accel_service_peek(&data);
		if (data.y < 0 && data.y > -500)
			anim_scroll_speed = 0;
		else if (data.y < -700)
			anim_scroll_speed = 200;
		else if (data.y <= -500)
			anim_scroll_speed = 50;
		else if (data.y >= 300)
			anim_scroll_speed = -350;
		else if (data.y >= 0)
			anim_scroll_speed = -50;
		if (settings_get_bool_flags() & SETTINGS_BOOL_LIGHT_WHILE_SCROLLING)
			light_enable_interaction();
	}
}

//Starts the continuous scrolling. Technically, that's an animation runs continuously, checking the accelerometer for instructions
void start_scroll_continuously() {
	static const AnimationImplementation my_implementation = {
	  .update = (AnimationUpdateImplementation) continuous_animation_impl
	};
	if (continuous_scroll_anim != 0) //animation still running
		return;

	//Start the scroll animation
    continuous_scroll_anim = animation_create();
	animation_set_duration((struct Animation*) continuous_scroll_anim, ANIMATION_DURATION_INFINITE);
	animation_set_implementation((struct Animation*) continuous_scroll_anim, &my_implementation);
    time_ms(&anim_last_milestone_s, &anim_last_milestone_ms);
	anim_scroll_speed = 20;
	anim_last_milestone_y = scroll_position;
	anim_num_milestones = 0;
	accel_data_service_subscribe(0, NULL);
	animation_schedule((Animation*) continuous_scroll_anim);
}

void vibrate(uint8_t type) {
	switch (type) {
		case 0:
			return;
		case 1:
			vibes_short_pulse();
			break;
		case 2:
			vibes_double_pulse();
			break;
		case 3:
			vibes_long_pulse();
			break;
	}
}

//Create all necessary structures, etc.
void handle_init(void) {
	//Init window
	window = window_create();
	window_stack_push(window, true);
 	window_set_background_color(window, GColorBlack);
	root_layer = layer_create(GRect(0,0,1,1));
	layer_set_clips(root_layer, false);
	layer_add_child(window_get_root_layer(window), root_layer);
	
	if (persist_exists(PERSIST_LAST_SYNC_ID)) {
		persist_read_data(PERSIST_LAST_SYNC_ID, &last_sync_id, sizeof(last_sync_id));
	}
	else
		last_sync_id = 0;
	
	db_restore_persisted();
	settings_restore_persisted();
	
	//Create some initial stuff depending on settings
	handle_new_settings();
	
	//Show data from database
	display_data();	
	
	//Register services
	tick_timer_service_subscribe(MINUTE_UNIT, &handle_time_tick);
	battery_state_service_subscribe(&handle_battery);
	bluetooth_connection_service_subscribe(bluetooth_connection_callback);
	
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
	accel_data_service_unsubscribe();
	tick_timer_service_unsubscribe();
	battery_state_service_unsubscribe();
	bluetooth_connection_service_unsubscribe();
	app_message_deregister_callbacks();
	
	//Destroy ui
	destroy_header();
	remove_displayed_data();
	layer_destroy(root_layer);
	if (inverter_layer != 0)
		inverter_layer_destroy(inverter_layer);
	window_destroy(window);
	
	//Unload font(s)
	if (time_font != 0)
		fonts_unload_custom_font(time_font);
	
	//Write persistent data
	if (settings_get_bool_flags() & SETTINGS_BOOL_LIMIT_PERSIST) {
		last_sync_id = 0; //force sync next open
		db_persist(5); //at most 5 elements persisted
	} else 
		db_persist(30);
	persist_write_data(PERSIST_LAST_SYNC_ID, &last_sync_id, sizeof(last_sync_id));
	//settings_persist(); //is persisted when new settings arrive
	
	//Destroy last references
	db_reset();
	communication_cleanup();
	scroll_cleanup();
	continuous_scroll_cleanup();
	if (scroll_reset_timer != 0)
		app_timer_cancel(scroll_reset_timer);
}

int main(void) {
	handle_init();
	app_event_loop();
	handle_deinit();
}
