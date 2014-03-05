#include <pebble.h>
#include <datatypes.h>
#include <event_db.h>
#include <main.h>
#include <communication.h>

//Version of the watchapp. Will be compared to what version the (updated) phone app expects
#define WATCHAPP_VERSION 6
#define BACKWARD_COMPAT_VERSION 4
//BACKWARD_COMPAT_VERSION smallest version number that this version is backwards compatible to (so an Android app bundling that (older) version would still work)
	
//Definitions of dictionary keys
#define DICT_KEY_COMMAND 0
#define DICT_KEY_VERSION 1
#define DICT_KEY_SYNC_ID 2
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
#define DICT_OUT_KEY_BACKWARDSVERSION 1
#define DICT_OUT_KEY_LAST_SYNC_ID 2

//Commands from phone
#define COMMAND_INIT_DATA 0
#define COMMAND_EVENT 1
#define COMMAND_DONE 2
#define COMMAND_EVENT_TIME 3
#define COMMAND_NO_NEW_DATA 4

CalendarEvent **buffer = 0; //buffered calendar events so far
uint8_t buffer_size = 0; //number of elements in the buffer (for cleanup)
uint8_t number_received = 0; //number of events completely received
uint8_t number_expected = 0; //number of events the phone said it will send
uint8_t current_sync_id = 0; //id of the current sync (as reported by phone)
bool expecting_event_time = 0; //if true, we expect the last received event's time now (second message)
bool update_request_sent = 0; //whether or not we informed the phone about outdated version

void send_sync_request(uint8_t report_sync_id) { //Sends a request for fresh data to the phone. Report report_sync_id as last successful sync (0 to force sync)
	APP_LOG(APP_LOG_LEVEL_DEBUG, "Sending sync request");
	DictionaryIterator *iter;
	app_message_outbox_begin(&iter);
	Tuplet value = TupletInteger(DICT_OUT_KEY_VERSION, WATCHAPP_VERSION);
	dict_write_tuplet(iter, &value);
	Tuplet value2 = TupletInteger(DICT_OUT_KEY_BACKWARDSVERSION, BACKWARD_COMPAT_VERSION);
	dict_write_tuplet(iter, &value2);
	Tuplet value3 = TupletInteger(DICT_OUT_KEY_LAST_SYNC_ID, report_sync_id);
	dict_write_tuplet(iter, &value3);
	app_message_outbox_send();
	sync_layer_set_progress(0,1);
}

void out_sent_handler(DictionaryIterator *sent, void *context) {
	// outgoing message was delivered
}


void out_failed_handler(DictionaryIterator *failed, AppMessageResult reason, void *context) {
	APP_LOG(APP_LOG_LEVEL_DEBUG, "Sending failed for reason %d", reason);
}


void in_received_handler(DictionaryIterator *received, void *context) {
	Tuple *command = dict_find(received, DICT_KEY_COMMAND);
	
	if (command) {
		//APP_LOG(APP_LOG_LEVEL_DEBUG, "Got message with command %d", (int) command->value->uint8);
		switch (command->value->uint8) {
			case COMMAND_INIT_DATA: //First message (starting new sync, giving settings)
			//Check version number
			if (dict_find(received, DICT_KEY_VERSION)->value->uint8 > WATCHAPP_VERSION) {
				if (!update_request_sent) {
					send_sync_request(0); //make sure the phone knows about our mismatched version
					update_request_sent = true; //do it only once
				}
				return; //ignore message
			}
			
			if (number_expected != 0) {
				communication_cleanup();
			}
			number_expected = dict_find(received, DICT_KEY_NUM_EVENTS)->value->uint8;
			current_sync_id = dict_find(received, DICT_KEY_SYNC_ID)->value->uint8;
			APP_LOG(APP_LOG_LEVEL_DEBUG, "starting with sync id %d", current_sync_id);
			if (number_expected != 0) {
				//init buffer
				number_received = 0;
				buffer_size = 0;
				buffer = malloc(sizeof(CalendarEvent*)*number_expected);
				
				APP_LOG(APP_LOG_LEVEL_DEBUG, "Starting sync. Expecting %d events", (int) number_expected);

				//Begin heightened communication status (for faster sync, hopefully)
				app_comm_set_sniff_interval(SNIFF_INTERVAL_REDUCED);
			
				//Show user
				sync_layer_set_progress(number_received+1, number_expected+2);
			} else {
				APP_LOG(APP_LOG_LEVEL_DEBUG, "Phone does not have any events to send");
				sync_layer_set_progress(0,0);
				handle_data_gone();
				event_db_reset();
				handle_new_data(current_sync_id);
			}
			
			//Apply settings from the message
			settings_set(dict_find(received, DICT_KEY_SETTINGS_BOOLFLAGS)->value->uint32, 
						 dict_find(received, DICT_KEY_SETTINGS_DESIGN)->value->uint32);
			break;
			
			case COMMAND_NO_NEW_DATA: //phone informs us that our data is up-to-date
				sync_layer_set_progress(0,0);
				handle_no_new_data();
			break;
			
			case COMMAND_EVENT: //getting first half of an event
			if (number_expected-number_received != 0 && number_expected != 0 && !expecting_event_time) { //check if message is expected
				//APP_LOG(APP_LOG_LEVEL_DEBUG, "Got event %s. Start time: %lu", dict_find(received, DICT_KEY_TITLE)->value->cstring, dict_find(received, 147851)->value->uint32);				
				buffer[number_received] = create_calendar_event();
				buffer_size++;
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
				sync_layer_set_progress(number_received+1, number_expected+2);
			}
			break;
		
			case COMMAND_DONE: //phone signals it sent all its data
			if (number_expected-number_received == 0 && number_expected != 0) { //is message expected?
				handle_data_gone(); //stop showing data
				event_db_reset(); //reset database
				
				for (int i=0;i<number_received;i++) //insert buffered events into database
					event_db_put(buffer[i]);
				
				handle_new_data(current_sync_id); //show new data
				
				//Reset to begin again
				free(buffer);
				buffer_size = 0;
				number_expected = 0;
				number_received = 0;
				expecting_event_time = 0;
				
				APP_LOG(APP_LOG_LEVEL_DEBUG, "Sync done");
				sync_layer_set_progress(0,0);
			}
			else {//phone thinks it's done but at some point, we began ignoring (yet ack'ing) its messages. So we request a restart
				send_sync_request(0);
				APP_LOG(APP_LOG_LEVEL_DEBUG, "Phone finished sync but something went wrong - requesting restart");
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
		for (int i=0;i<buffer_size;i++)
			free(buffer[i]);
		free(buffer);
		
		buffer_size = 0;
		number_expected = 0;
		number_received = 0;
		expecting_event_time = 0;
	}
}