#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"

/* ------------------------------------------------------------------ */
/*  Challenge 1: hashValue()                                           */
/*                                                                      */
/*  Produce a uint32_t hash for any Value type.                        */
/*                                                                      */
/*  VAL_NIL    → constant (there is only one nil)                     */
/*  VAL_BOOL   → 1 for true, 0 for false                              */
/*  VAL_NUMBER → bit-cast the double to uint64_t, fold the two        */
/*               32-bit halves together with XOR.  This preserves the  */
/*               invariant that equal values hash equally, including   */
/*               the tricky case 0.0 == -0.0 in IEEE 754 (they have  */
/*               different bit patterns, but we treat them as the same */
/*               value, so we normalise -0.0 → 0.0 before hashing).  */
/*  VAL_OBJ    → use the pre-computed hash stored in ObjString.       */
/*               For future object types without a stored hash, we     */
/*               fall back to hashing the pointer address.             */
/* ------------------------------------------------------------------ */

static uint32_t hashValue(Value value) {
	  switch (value.type) {
		      case VAL_NIL:    return 0;
				           case VAL_BOOL:   return AS_BOOL(value) ? 1 : 0;
							        case VAL_NUMBER: {
											       double n = AS_NUMBER(value);
											             /* Normalise negative zero so 0.0 and -0.0 hash identically */
											             if (n == 0.0) n = 0.0;
												           /* Bit-cast to uint64_t — same bytes, different type */
												           uint64_t bits;
													         memcpy(&bits, &n, sizeof(bits));
														       /* Fold upper and lower 32 bits */
														       return (uint32_t)(bits ^ (bits >> 32));
														           }
							        case VAL_OBJ: {
										            if (IS_STRING(value)) return AS_STRING(value)->hash;
											          /* For other objects: hash the pointer address */
											          uintptr_t addr = (uintptr_t)AS_OBJ(value);
												        return (uint32_t)(addr ^ (addr >> 32));
													    }
									          default: return 0;
											     }
}

/* ------------------------------------------------------------------ */
/*  isValidKey — nil is a valid key (there's only one),               */
/*  but we must distinguish "empty bucket" from "nil key".             */
/*  We use a separate sentinel for empty buckets (see below).         */
/* ------------------------------------------------------------------ */

/* Sentinel value used to mark an empty bucket (not a tombstone).
 *  * We use a special double NaN that can't arise from normal Lox code. */
#define EMPTY_KEY   ((Value){VAL_NIL, {.number = 0}})
/* Tombstone: NULL obj pointer in a VAL_OBJ slot is our sentinel.    */
#define TOMBSTONE_KEY ((Value){VAL_BOOL, {.boolean = false}})

/* We need to distinguish:
 *  *   empty bucket  → key.type == VAL_NIL  && key.as.number == 0
 *   *   tombstone     → key.type == VAL_BOOL && key.as.boolean == false
 *    *   nil user key  → key.type == VAL_NIL  && key.as.number == 1  (we tag it)
 *     *
 *      * Actually the simplest approach: use a bool `occupied` flag per
 *       * bucket, or use a dedicated EMPTY sentinel distinct from nil.
 *        *
 *         * We'll use a 3-state tag stored in entry->key:
 *          *   type == VAL_NUMBER, number == 0/1  would collide with real numbers.
 *           *
 *            * Cleanest solution: use a dedicated internal type tag for sentinels.
 *             * We add VAL_EMPTY to the ValueType enum — but that touches value.h.
 *              *
 *               * Simpler for this challenge: store an `occupied` bool in Entry.
 *                */

static bool isEmpty(Entry* e)    { return !e->occupied && !e->tombstone; }
static bool isTombstone(Entry* e){ return e->tombstone; }

/* ------------------------------------------------------------------ */

void initTable(Table* table) {
	  table->count    = 0;
	    table->capacity = 0;
	      table->entries  = NULL;
}

void freeTable(Table* table) {
	  FREE_ARRAY(Entry, table->entries, table->capacity);
	    initTable(table);
}

/* ------------------------------------------------------------------ */
/*  findEntry — linear probe, handles tombstones                      */
/* ------------------------------------------------------------------ */

static Entry* findEntry(Entry* entries, int capacity, Value key) {
	  if (capacity == 0) return NULL;
	    uint32_t index    = hashValue(key) % (uint32_t)capacity;
	      Entry*   tombstone = NULL;

	        for (;;) {
			    Entry* entry = &entries[index];

			        if (isEmpty(entry)) {
					      return tombstone != NULL ? tombstone : entry;
					          } else if (isTombstone(entry)) {
							        if (tombstone == NULL) tombstone = entry;
								    } else if (valuesEqual(entry->key, key)) {
									          return entry;
										      }

				    index = (index + 1) % (uint32_t)capacity;
				      }
}

/* ------------------------------------------------------------------ */
/*  adjustCapacity                                                      */
/* ------------------------------------------------------------------ */

static void adjustCapacity(Table* table, int capacity) {
	  Entry* entries = ALLOCATE(Entry, capacity);
	    for (int i = 0; i < capacity; i++) {
		        entries[i].occupied  = false;
			    entries[i].tombstone = false;
			      }

	      table->count = 0;
	        for (int i = 0; i < table->capacity; i++) {
			    Entry* src = &table->entries[i];
			        if (!src->occupied || src->tombstone) continue;

				    Entry* dest = findEntry(entries, capacity, src->key);
				        dest->key      = src->key;
					    dest->value    = src->value;
					        dest->occupied  = true;
						    dest->tombstone = false;
						        table->count++;
							  }

		  FREE_ARRAY(Entry, table->entries, table->capacity);
		    table->entries  = entries;
		      table->capacity = capacity;
}

/* ------------------------------------------------------------------ */
/*  tableGet                                                            */
/* ------------------------------------------------------------------ */

bool tableGet(Table* table, Value key, Value* value) {
	  if (table->count == 0) return false;
	    Entry* entry = findEntry(table->entries, table->capacity, key);
	      if (!entry->occupied || entry->tombstone) return false;
	        *value = entry->value;
		  return true;
}

/* ------------------------------------------------------------------ */
/*  tableSet                                                            */
/* ------------------------------------------------------------------ */

bool tableSet(Table* table, Value key, Value value) {
	  if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
		      int cap = GROW_CAPACITY(table->capacity);
		          adjustCapacity(table, cap);
			    }

	    Entry* entry    = findEntry(table->entries, table->capacity, key);
	      bool   isNewKey = !entry->occupied || entry->tombstone;
	        if (isNewKey && !entry->tombstone) table->count++;

		  entry->key      = key;
		    entry->value    = value;
		      entry->occupied  = true;
		        entry->tombstone = false;
			  return isNewKey;
}

/* ------------------------------------------------------------------ */
/*  tableDelete                                                         */
/* ------------------------------------------------------------------ */

bool tableDelete(Table* table, Value key) {
	  if (table->count == 0) return false;
	    Entry* entry = findEntry(table->entries, table->capacity, key);
	      if (!entry->occupied || entry->tombstone) return false;
	        entry->tombstone = true;
		  return true;
}

/* ------------------------------------------------------------------ */
/*  tableAddAll                                                         */
/* ------------------------------------------------------------------ */

void tableAddAll(Table* from, Table* to) {
	  for (int i = 0; i < from->capacity; i++) {
		      Entry* e = &from->entries[i];
		          if (e->occupied && !e->tombstone) {
				        tableSet(to, e->key, e->value);
					    }
			    }
}

/* ------------------------------------------------------------------ */
/*  tableFindString — still used by the string intern pool             */
/*  Keys in the intern pool are always ObjString* wrapped in OBJ_VAL  */
/* ------------------------------------------------------------------ */

ObjString* tableFindString(Table* table, const char* chars,
		                           int length, uint32_t hash) {
	  if (table->count == 0) return NULL;
	    uint32_t index = hash % (uint32_t)table->capacity;

	      for (;;) {
		          Entry* entry = &table->entries[index];
			      if (isEmpty(entry)) return NULL;

			          if (!isTombstone(entry) && IS_STRING(entry->key)) {
					        ObjString* s = AS_STRING(entry->key);
						      if (s->length == length && s->hash == hash &&
								                memcmp(s->chars, chars, length) == 0) {
							              return s;
								            }
						          }
				      index = (index + 1) % (uint32_t)table->capacity;
				        }
}
