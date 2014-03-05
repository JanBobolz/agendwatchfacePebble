#include <pebble.h>

#ifndef COMM_H
#define COMM_H

//For comments, see communication.c
void send_sync_request(uint8_t report_sync_id);
void out_sent_handler(DictionaryIterator *sent, void *context);
void out_failed_handler(DictionaryIterator *failed, AppMessageResult reason, void *context);
void in_received_handler(DictionaryIterator *received, void *context);
void in_dropped_handler(AppMessageResult reason, void *context);
void communication_cleanup();

#endif