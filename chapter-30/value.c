#include <stdio.h>
#include <string.h>
#include "memory.h"
#include "object.h"
#include "value.h"

void initValueArray(ValueArray* a){a->values=NULL;a->capacity=0;a->count=0;}
void writeValueArray(ValueArray* a,Value value){
  if(a->capacity<a->count+1){int old=a->capacity;a->capacity=GROW_CAPACITY(old);a->values=GROW_ARRAY(Value,a->values,old,a->capacity);}
  a->values[a->count++]=value;
}
void freeValueArray(ValueArray* a){FREE_ARRAY(Value,a->values,a->capacity);initValueArray(a);}

void printValue(Value value){
  switch(value.type){
    case VAL_BOOL:printf(AS_BOOL(value)?"true":"false");break;
    case VAL_NIL:printf("nil");break;
    case VAL_NUMBER:printf("%g",AS_NUMBER(value));break;
    case VAL_OBJ:printObject(value);break;
    case VAL_SHORT_STRING:printf("%.*s",SSO_LENGTH(value),SSO_CHARS(value));break;
  }
}

/* Helper: get string chars+len from any string Value */
static bool getStringContents(Value v, const char** chars, int* length) {
  if (v.type == VAL_SHORT_STRING) {
    *chars  = SSO_CHARS(v);
    *length = SSO_LENGTH(v);
    return true;
  }
  if (v.type == VAL_OBJ && OBJ_GET_TYPE(AS_OBJ(v)) == OBJ_STRING) {
    ObjString* s = (ObjString*)AS_OBJ(v);
    *chars  = s->chars;
    *length = s->length;
    return true;
  }
  return false;
}

bool valuesEqual(Value a, Value b) {
  /* Cross-type string comparison: SSO Value vs heap ObjString */
  const char* aChars; int aLen;
  const char* bChars; int bLen;
  bool aIsStr = getStringContents(a, &aChars, &aLen);
  bool bIsStr = getStringContents(b, &bChars, &bLen);
  if (aIsStr && bIsStr) {
    if (aLen != bLen) return false;
    return memcmp(aChars, bChars, aLen) == 0;
  }

  if (a.type != b.type) return false;
  switch(a.type){
    case VAL_BOOL:   return AS_BOOL(a)==AS_BOOL(b);
    case VAL_NIL:    return true;
    case VAL_NUMBER: return AS_NUMBER(a)==AS_NUMBER(b);
    case VAL_OBJ:    return AS_OBJ(a)==AS_OBJ(b);
    case VAL_SHORT_STRING:
      if(SSO_LENGTH(a)!=SSO_LENGTH(b))return false;
      return memcmp(SSO_CHARS(a),SSO_CHARS(b),SSO_LENGTH(a))==0;
    default:return false;
  }
}

bool strValContents(Value v, const char** chars, int* length) {
  if (v.type == VAL_SHORT_STRING) {
    *chars  = v.as.shortStr;
    *length = (int)(uint8_t)v.as.shortStr[SSO_MAX];
    return true;
  }
  if (v.type == VAL_OBJ && OBJ_GET_TYPE(AS_OBJ(v)) == OBJ_STRING) {
    ObjString* s = (ObjString*)AS_OBJ(v);
    *chars  = s->chars;
    *length = s->length;
    return true;
  }
  return false;
}
bool isStrVal(Value v) {
  if (v.type == VAL_SHORT_STRING) return true;
  if (v.type == VAL_OBJ && OBJ_GET_TYPE(AS_OBJ(v)) == OBJ_STRING) return true;
  return false;
}
