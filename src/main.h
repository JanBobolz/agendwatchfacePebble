#ifndef MAIN_H
#define MAIN_H

void handle_data_gone();
void handle_new_data(uint8_t sync_id);
void handle_no_new_data();
void handle_new_settings();
void sync_layer_set_progress(int now, int max);
void scroll(int y);
void vibrate(uint8_t type);
#endif