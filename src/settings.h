#include<pebble.h>
	
#ifndef SETTINGS_H
#define SETTINGS_H
	
//Show the header with date and clock?
#define SETTINGS_BOOL_SHOW_CLOCK_HEADER 0x01

//12h time for event times
#define SETTINGS_BOOL_12H 0x02

//Append am/pm to time (if 12h is activated)
#define SETTINGS_BOOL_AMPM 0x04
	
//First bit of font size id
#define SETTINGS_BOOL_FONT_SIZE0 0x20

//Second bit of font size id
#define SETTINGS_BOOL_FONT_SIZE1 0x40

//Size of time in the header (lower bit)
#define SETTINGS_BOOL_HEADER_SIZE0 0x80

//Size of time in the header (higher bit)
#define SETTINGS_BOOL_HEADER_SIZE1 0x100
	
//Whether or not to show the date in the separator layers
#define SETTINGS_BOOL_SEPARATOR_DATE 0x200
	
//Whether or not to scroll on accelerometer tap
#define SETTINGS_BOOL_ENABLE_SCROLL 0x400

//Whether or not to display the remaining minutes instead of the time in events
#define SETTINGS_BOOL_COUNTDOWNS 0x800

//Whether or not to use the new scrolling paradigm (tap, then scroll)
#define SETTINGS_BOOL_ENABLED_ALT_SCROLL 0x1000
	
void settings_persist(); //saves settings to persistent storage. (Does not have to be called from outside settings.c)
void settings_restore_persisted(); //restores settings from persistent storage (if exists)

uint32_t settings_get_bool_flags(); //getter for general settings
void settings_set(uint32_t bool_flags); //setter for both. Will callback to main.h and persist on changes

#endif