#ifndef clox_table_h
#define clox_table_h

#include "common.h"
#include "value.h"

#define TABLE_MAX_LOAD 0.75

/*
 *  * Challenge 1: Entry uses Value as key instead of ObjString*.
 *   *
 *    * We add two sentinel booleans to distinguish three bucket states:
 *     *   occupied=false, tombstone=false  → empty bucket
 *      *   occupied=true,  tombstone=false  → live entry
 *       *   occupied=true,  tombstone=true   → deleted (tombstone)
 *        *
 *         * This cleanly handles nil as a valid user key (VAL_NIL in key field)
 *          * without any ambiguity about what "empty" means.
 *           */
typedef struct {
	  Value key;
	    Value value;
	      bool  occupied;
	        bool  tombstone;
} Entry;

typedef struct {
	  int    count;
	    int    capacity;
	      Entry* entries;
} Table;

void       initTable      (Table* table);
void       freeTable      (Table* table);
bool       tableGet       (Table* table, Value key, Value* value);
bool       tableSet       (Table* table, Value key, Value value);
bool       tableDelete    (Table* table, Value key);
void       tableAddAll    (Table* from, Table* to);
ObjString* tableFindString(Table* table, const char* chars,
		                           int length, uint32_t hash);

#endif
