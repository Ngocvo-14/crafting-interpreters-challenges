#include <stdlib.h>
#include <string.h>
#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"

#define TABLE_MAX_LOAD 0.75

void initTable(Table* t) { t->count=0; t->capacity=0; t->entries=NULL; }
void freeTable(Table* t) { FREE_ARRAY(Entry,t->entries,t->capacity); initTable(t); }

static Entry* findEntry(Entry* entries, int capacity, ObjString* key) {
  uint32_t index = key->hash % (uint32_t)capacity;
  Entry* tombstone = NULL;
  for(;;){
    Entry* e=&entries[index];
    if(e->key==NULL){if(IS_NIL(e->value))return tombstone?tombstone:e;else if(!tombstone)tombstone=e;}
    else if(e->key==key){return e;}
    index=(index+1)%(uint32_t)capacity;
  }
}

bool tableGet(Table* t, ObjString* key, Value* value) {
  if(t->count==0)return false;
  Entry* e=findEntry(t->entries,t->capacity,key);
  if(!e->key)return false;
  *value=e->value;return true;
}

static void adjustCapacity(Table* t, int capacity) {
  Entry* entries=ALLOCATE(Entry,capacity);
  for(int i=0;i<capacity;i++){entries[i].key=NULL;entries[i].value=NIL_VAL;}
  t->count=0;
  for(int i=0;i<t->capacity;i++){
    Entry* e=&t->entries[i];if(!e->key)continue;
    Entry* dest=findEntry(entries,capacity,e->key);
    dest->key=e->key;dest->value=e->value;t->count++;
  }
  FREE_ARRAY(Entry,t->entries,t->capacity);
  t->entries=entries;t->capacity=capacity;
}

bool tableSet(Table* t, ObjString* key, Value value) {
  if(t->count+1>t->capacity*TABLE_MAX_LOAD)adjustCapacity(t,GROW_CAPACITY(t->capacity));
  Entry* e=findEntry(t->entries,t->capacity,key);
  bool isNew=(e->key==NULL);
  if(isNew&&IS_NIL(e->value))t->count++;
  e->key=key;e->value=value;return isNew;
}

bool tableDelete(Table* t, ObjString* key) {
  if(t->count==0)return false;
  Entry* e=findEntry(t->entries,t->capacity,key);
  if(!e->key)return false;
  e->key=NULL;e->value=BOOL_VAL(true);return true;
}

void tableAddAll(Table* from, Table* to) {
  for(int i=0;i<from->capacity;i++){Entry* e=&from->entries[i];if(e->key)tableSet(to,e->key,e->value);}
}

ObjString* tableFindString(Table* t, const char* chars, int length, uint32_t hash) {
  if(t->count==0)return NULL;
  uint32_t index=hash%(uint32_t)t->capacity;
  for(;;){
    Entry* e=&t->entries[index];
    if(e->key==NULL){if(IS_NIL(e->value))return NULL;}
    else if(e->key->length==length&&e->key->hash==hash&&memcmp(e->key->chars,chars,length)==0)return e->key;
    index=(index+1)%(uint32_t)t->capacity;
  }
}

/* ch26: remove string entries whose key is not marked (weak references) */
void tableRemoveWhite(Table* t) {
  for(int i=0;i<t->capacity;i++){
    Entry* e=&t->entries[i];
    if(e->key!=NULL && OBJ_IS_MARKED((Obj*)e->key) != vm.markBit)
      tableDelete(t, e->key);
  }
}

/* ch26: mark all keys and values in a table */
void markTable(Table* t) {
  for(int i=0;i<t->capacity;i++){
    Entry* e=&t->entries[i];
    markObject((Obj*)e->key);
    markValue(e->value);
  }
}
