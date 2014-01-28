#include <pebble.h>
#include <datatypes.h>
#include <event_db.h>
#include <main.h>
#include <communication.h>

//Version of the watchapp. Will be compared to what version the (updated) phone app expects
#define WATCHAPP_VERSION 4
	
//Definitions of dictionary keys
#define DICT_KEY_COMMAND 0
#define DICT_KEY_VERSION 1
#define DICT_KEY_NUM_EVENTS 10
#define DICT_KEY_TITLE 1
#define DICT_KEY_LOCATION 2
#define DICT_KEY_STARTTIME 20
#define DICT_KEY_ENDTIME 30
#define DICT_KEY_ALLDAY 5
#define DICT_KEY_SETTINGS_BOOLFLAGS 40
#define DICT_KEY_SETTINGS_DESIGN 41

//Outgoing dictionary keys
#define DICT_OUT_KEY_VERSION 0

//Commands from phone
#define COMMAND_INIT_DATA 0
#define COMMAND_EVENT 1
#define COMMAND_DONE 2
#define COMMAND_EVENT_TIME 3

CalendarEvent **buffer = 0; //buffered calendar events so far
uint8_t number_received = 0; //number of events completely received
uint8_t number_expected = 0; //number of events the phone said it will send
bool expecting_event_time = 0; //if true, we expect the last received event's time (second message)
bool update_request_sent = 0; //whether or not we informed the phone about outdated version

void send_sync_request() { //Sends a request for fresh data to the phone
	APP_LOG(APP_LOG_LEVEL_DEBUG, "Sending sync request");
	DictionaryIterator *iter;
	app_message_outbox_begin(&iter);
	Tuplet value = TupletInteger(DICT_OUT_KEY_VERSION, WATCHAPP_VERSION);
	dict_write_tuplet(iter, &value);
	app_message_outbox_send();
}

void out_sent_handler(DictionaryIterator *sent, void *context) {
	// outgoing message was delivered
}


void out_failed_handler(DictionaryIterator *failed, AppMessageResult reason, void *context) {
	// outgoing message failed
}


void in_received_handler(DictionaryIterator *received, void *context) {
	Tuple *command = dict_find(received, DICT_KEY_COMMAND);
	
	if (command) {
		//APP_LOG(APP_LOG_LEVEL_DEBUG, "Got message with command %d", (int) command->value->uint8);
		switch (command->value->uint8) {
			case COMMAND_INIT_DATA: //First message (starting new sync, giving settings)
			//Check version number
			if (dict_find(received, DICT_KEY_VERSION)->value->uint8 != WATCHAPP_VERSION) {
				if (!update_request_sent) {
					send_sync_request(); //make sure the phone knows about our mismatched version
					update_request_sent = true; //do it only once
				}
				return; //ignore message
			}
			
			if (number_expected != 0) {
				communication_cleanup();
			}
			number_expected = dict_find(received, DICT_KEY_NUM_EVENTS)->value->uint8;
			if (number_expected != 0) {
				//init buffer
				number_received = 0;
				buffer = malloc(sizeof(CalendarEvent*)*number_expected);
				APP_LOG(APP_LOG_LEVEL_DEBUG, "Starting sync. Expecting %d events", (int) number_expected);
			}
			
			//Apply settings from the message
			settings_set_design(dict_find(received, DICT_KEY_SETTINGS_DESIGN)->value->uint32);
			settings_set_bool_flags(dict_find(received, DICT_KEY_SETTINGS_BOOLFLAGS)->value->uint32);
			
			//Begin heightened communication status (for faster sync, hopefully)
			app_comm_set_sniff_interval(SNIFF_INTERVAL_REDUCED);
			break;
			
			case COMMAND_EVENT: //getting first half of an event
			if (number_expected-number_received != 0 && number_expected != 0 && !expecting_event_time) { //check if message is expected
				//APP_LOG(APP_LOG_LEVEL_DEBUG, "Got event %s. Start time: %lu", dict_find(received, DICT_KEY_TITLE)->value->cstring, dict_find(received, 147851)->value->uint32);				
				buffer[number_received] = create_calendar_event();
				cal_set_title_and_loc(buffer[number_received], dict_find(received, DICT_KEY_TITLE)->value->cstring, dict_find(received, DICT_KEY_LOCATION)->value->cstring);
				cal_set_allday(buffer[number_received], dict_find(received, DICT_KEY_ALLDAY)->value->uint8 ? 1 : 0);				
				expecting_event_time = 1; //state now: waiting for second half
			}
			break;
			
			case COMMAND_EVENT_TIME: //getting second half of an event
			if (number_expected-number_received != 0 && number_expected != 0 && expecting_event_time) { //message expected?
				//Add information to calendar event
				cal_set_start_time(buffer[number_received], dict_find(received, DICT_KEY_STARTTIME)->value->int32);
				cal_set_end_time(buffer[number_received], dict_find(received, DICT_KEY_ENDTIME)->value->int32);
				//APP_LOG(APP_LOG_LEVEL_DEBUG, "Got event time data for %s", buffer[number_received]->title);
				number_received++;
				expecting_event_time = 0; //state: awaiting another first half (or done message)
			}
			break;
		
			case COMMAND_DONE: //phone signals it sent all its data
			if (number_expected-number_received == 0 && number_expected != 0) { //is message expected?
				handle_data_gone(); //stop showing data
				event_db_reset(); //reset database
				
				for (int i=0;i<number_received;i++) //insert buffered events into database
					event_db_put(buffer[i]);
				
				handle_new_data(); //show new data
				
				//Reset to begin again
				free(buffer);
				number_expected = 0;
				number_received = 0;
				expecting_event_time = 0;
				
				APP_LOG(APP_LOG_LEVEL_DEBUG, "Sync done");
			}
			app_comm_set_sniff_interval(SNIFF_INTERVAL_NORMAL); //stop heightened communcation
			break;
		}
	}
}

void in_dropped_handler(AppMessageResult reason, void *context) { //incoming message dropped: just reset whole process (simple protocol...)
	communication_cleanup();
	APP_LOG(APP_LOG_LEVEL_WARNING, "inbound message dropped (reason %d)", (int) reason);
}

void communication_cleanup() { //reset everything to start state (also cleans up malloc'ed memory)
	if (number_expected != 0) {
		for (int i=0;i<number_received;i++)
			free(buffer[i]);
		free(buffer);
		
		number_expected = 0;
		number_received = 0;
		expecting_event_time = 0;
	}
}