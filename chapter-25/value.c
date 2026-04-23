#include <stdio.h>
#include <string.h>
#include "memory.h"
#include "object.h"
#include "value.h"

void initValueArray(ValueArray* a)  { a->values=NULL; a->capacity=0; a->count=0; }
void freeValueArray(ValueArray* a)  { FREE_ARRAY(Value,a->values,a->capacity); initValueArray(a); }
void writeValueArray(ValueArray* a, Value v) {
  if (a->capacity < a->count+1) {
    int old=a->capacity; a->capacity=GROW_CAPACITY(old);
    a->values=GROW_ARRAY(Value,a->values,old,a->capacity);
  }
  a->values[a->count++]=v;
}
void printValue(Value v) {
  switch(v.type) {
    case VAL_BOOL:   printf(AS_BOOL(v)?"true":"false"); break;
    case VAL_NIL:    printf("nil"); break;
    case VAL_NUMBER: printf("%g", AS_NUMBER(v)); break;
    case VAL_OBJ:    printObject(v); break;
  }
}
bool valuesEqual(Value a, Value b) {
  if (a.type != b.type) return false;
  switch(a.type) {
    case VAL_BOOL:   return AS_BOOL(a)==AS_BOOL(b);
    case VAL_NIL:    return true;
    case VAL_NUMBER: return AS_NUMBER(a)==AS_NUMBER(b);
    case VAL_OBJ:    return AS_OBJ(a)==AS_OBJ(b);
    default:         return false;
  }
}
