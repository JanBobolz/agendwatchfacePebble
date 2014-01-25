#include <pebble.h>
#ifndef PERSIST_CONST_H
#define PERSIST_CONST_H

//Persistent storage constants	

//Last sync
#define PERSIST_LAST_SYNC 0

//Settings
#define PERSIST_BOOL_FLAG_SETTINGS 0x11001
#define PERSIST_DESIGN_SETTINGS 0x11002

//Events
#define PERSIST_NUM_ELEMS 3
#define PERSIST_CAL_PREFIX 0x1000
//notice that this is simply the first persist id. Event i will be stored at 0x1000|i

#endif