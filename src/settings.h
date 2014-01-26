#include<pebble.h>
	
#ifndef SETTINGS_H
#define SETTINGS_H
	
//Show the header with date and clock?
#define SETTINGS_BOOL_SHOW_CLOCK_HEADER 0x01

//12h time for event times
#define SETTINGS_BOOL_12H 0x02

//Append am/pm to time (if 12h is activated)
#define SETTINGS_BOOL_AMPM 0x04

//Show second row for regular events
#define SETTINGS_BOOL_SHOW_ROW2 0x08

//Show second row for all-day events
#define SETTINGS_BOOL_AD_SHOW_ROW2 0x10

//First bit of font size id
#define SETTINGS_BOOL_FONT_SIZE0 0x20

//Second bit of font size id
#define SETTINGS_BOOL_FONT_SIZE1 0x40
	
//Size of time in the header (lower bit)
#define SETTINGS_BOOL_HEADER_SIZE0 0x80

//Size of time in the header (higher bit)
#define SETTINGS_BOOL_HEADER_SIZE1 0x100
	
void settings_persist(); //saves settings to persistent storage.
void settings_restore_persisted(); //restores settings from persistent storage (if exists)

uint32_t settings_get_bool_flags(); //getter for general settings
void settings_set_bool_flags(uint32_t flags);
uint32_t settings_get_design(); //getter for design settings
void settings_set_design(uint32_t design);

#endif