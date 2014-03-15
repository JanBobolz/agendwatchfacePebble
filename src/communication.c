#include <pebble.h>
#include <datatypes.h>
#include <item_db.h>
#include <main.h>
#include <communication.h>

//Version of the watchapp. Will be compared to what version the (updated) phone app expects
#define WATCHAPP_VERSION 8
#define BACKWARD_COMPAT_VERSION 8
//BACKWARD_COMPAT_VERSION smallest version number that this version is backwards compatible to (so an Android app bundling that (older) version would still work)
	
//Definitions of dictionary keys
#define DICT_KEY_COMMAND 0
#define DICT_KEY_VERSION 1
#define DICT_KEY_SYNC_ID 2
#define DICT_KEY_NUM_ITEMS 10
#define DICT_KEY_ITEM_TEXT1 1
#define DICT_KEY_ITEM_TEXT2 2
#define DICT_KEY_ITEM_DESIGN1 3
#define DICT_KEY_ITEM_DESIGN2 4
#define DICT_KEY_ITEM_STARTTIME 20
#define DICT_KEY_ITEM_ENDTIME 30
#define DICT_KEY_ITEM_INDEX 5
#define DICT_KEY_SETTINGS_BOOLFLAGS 40
#define DICT_KEY_VIBRATE 6

//Outgoing dictionary keys
#define DICT_OUT_KEY_VERSION 0
#define DICT_OUT_KEY_BACKWARDSVERSION 1
#define DICT_OUT_KEY_LAST_SYNC_ID 2

//Commands from phone
#define COMMAND_INIT_DATA 0
#define COMMAND_ITEM 1
#define COMMAND_DONE 2
#define COMMAND_NO_NEW_DATA 4
#define COMMAND_FORCE_REQUEST 5
#define COMMAND_ITEM_1 6
#define COMMAND_ITEM_2 7

AgendaItem **buffer = 0; //buffered items so far
uint8_t buffer_size = 0; //number of elements in the buffer (for cleanup)
uint8_t number_received = 0; //number of items completely received
uint8_t number_expected = 0; //number of items the phone said it will send
uint8_t index_expected = 0; //index that the next received item should carry (other messages will be ignored)
uint8_t current_sync_id = 0; //id of the current sync (as reported by phone)
bool expecting_second_half = false; //true if we still need the second half of the current item
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
			number_expected = dict_find(received, DICT_KEY_NUM_ITEMS)->value->uint8;
			Tuple* sync_id_tuple = dict_find(received, DICT_KEY_SYNC_ID);
			if (sync_id_tuple != NULL)
				current_sync_id = sync_id_tuple->value->uint8;
			APP_LOG(APP_LOG_LEVEL_DEBUG, "starting with sync id %d", current_sync_id);
			if (number_expected != 0) {
				//init buffer
				number_received = 0;
				index_expected = 0;
				buffer_size = 0;
				buffer = malloc(sizeof(AgendaItem*)*number_expected);
				
				APP_LOG(APP_LOG_LEVEL_DEBUG, "Starting sync. Expecting %d items", (int) number_expected);

				//Begin heightened communication status (for faster sync, hopefully)
				app_comm_set_sniff_interval(SNIFF_INTERVAL_REDUCED);
			
				//Show user
				sync_layer_set_progress(number_received+1, number_expected+2);
			} else {
				APP_LOG(APP_LOG_LEVEL_DEBUG, "Phone does not have any items to send");
				sync_layer_set_progress(0,0);
				handle_data_gone();
				db_reset();
				handle_new_data(current_sync_id);
			}
			
			//Apply settings from the message
			settings_set(dict_find(received, DICT_KEY_SETTINGS_BOOLFLAGS)->value->uint32);
			break;
			
			case COMMAND_NO_NEW_DATA: //phone informs us that our data is up-to-date
				sync_layer_set_progress(0,0);
				handle_no_new_data();
			break;
			
			case COMMAND_ITEM: //getting an item
			if (number_expected-number_received != 0 && number_expected != 0) { //check if message is expected				
				if (index_expected != dict_find(received, DICT_KEY_ITEM_INDEX)->value->uint8) {
					APP_LOG(APP_LOG_LEVEL_DEBUG, "got unexpected event (wrong index). Ignoring");
					break;
				}
				
				buffer[number_received] = create_agenda_item();
				buffer_size++;
				set_item_row1(buffer[number_received], dict_find(received, DICT_KEY_ITEM_TEXT1)->value->cstring, dict_find(received, DICT_KEY_ITEM_DESIGN1)->value->uint8);
				set_item_row2(buffer[number_received], dict_find(received, DICT_KEY_ITEM_TEXT2)->value->cstring, dict_find(received, DICT_KEY_ITEM_DESIGN2)->value->uint8);
				set_item_times(buffer[number_received], dict_find(received, DICT_KEY_ITEM_STARTTIME)->value->int32, dict_find(received, DICT_KEY_ITEM_ENDTIME)->value->int32);
				number_received++;
				index_expected++;
				expecting_second_half = false;
				sync_layer_set_progress(number_received+1, number_expected+2);
			}
			break;
			
			case COMMAND_ITEM_1: //getting an item half
			if (number_expected-number_received != 0 && number_expected != 0) { //check if message is expected				
				if (index_expected != dict_find(received, DICT_KEY_ITEM_INDEX)->value->uint8) {
					APP_LOG(APP_LOG_LEVEL_DEBUG, "got unexpected event (wrong index). Ignoring");
					break;
				}
				
				buffer[number_received] = create_agenda_item();
				buffer_size++;
				set_item_row1(buffer[number_received], dict_find(received, DICT_KEY_ITEM_TEXT1)->value->cstring, dict_find(received, DICT_KEY_ITEM_DESIGN1)->value->uint8);
				set_item_start_time(buffer[number_received], dict_find(received, DICT_KEY_ITEM_STARTTIME)->value->int32);				
				expecting_second_half = true;
			}
			break;
			
			case COMMAND_ITEM_2: //getting second item half
			if (number_expected-number_received != 0 && number_expected != 0) { //check if message is expected				
				if (index_expected != dict_find(received, DICT_KEY_ITEM_INDEX)->value->uint8 || !expecting_second_half) {
					APP_LOG(APP_LOG_LEVEL_DEBUG, "got unexpected event (wrong index/not expecting second half). Ignoring");
					break;
				}
				
				set_item_row2(buffer[number_received], dict_find(received, DICT_KEY_ITEM_TEXT2)->value->cstring, dict_find(received, DICT_KEY_ITEM_DESIGN2)->value->uint8);
				set_item_end_time(buffer[number_received], dict_find(received, DICT_KEY_ITEM_ENDTIME)->value->int32);
				number_received++;
				index_expected++;
				expecting_second_half = false;
				sync_layer_set_progress(number_received+1, number_expected+2);
			}
			break;
			
			case COMMAND_DONE: //phone signals it sent all its data
			if (number_expected-number_received == 0 && number_expected != 0) { //is message expected?
				handle_data_gone(); //stop showing data
				db_reset(); //reset database
				
				for (int i=0;i<number_received;i++) //insert buffered items into database
					db_put(buffer[i]);
				
				handle_new_data(current_sync_id); //show new data, remember the sync_id
				
				//Reset to begin again
				free(buffer);
				buffer_size = 0;
				number_expected = 0;
				number_received = 0;
				index_expected = 0;
				
				APP_LOG(APP_LOG_LEVEL_DEBUG, "Sync done");
				sync_layer_set_progress(0,0);
				vibrate(dict_find(received, DICT_KEY_VIBRATE)->value->uint8);
			}
			else {//phone thinks it's done but at some point, we began ignoring (yet ack'ing) its messages. So we request a restart
				send_sync_request(0);
				APP_LOG(APP_LOG_LEVEL_DEBUG, "Phone finished sync but something went wrong - requesting restart");
			}
			app_comm_set_sniff_interval(SNIFF_INTERVAL_NORMAL); //stop heightened communcation
			break;
			
			case COMMAND_FORCE_REQUEST: //the phone wants us to request an update (so that we report our version, etc.)
			APP_LOG(APP_LOG_LEVEL_DEBUG, "Got FORCE_REQUEST");
			send_sync_request(0);
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
		number_expected = 0;
	}
}