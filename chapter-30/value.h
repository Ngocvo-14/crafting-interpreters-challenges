#ifndef clox_value_h
#define clox_value_h
#include <string.h>
#include "common.h"

typedef struct Obj      Obj;
typedef struct ObjString ObjString;

#define SSO_MAX 7

typedef enum {
  VAL_BOOL,
  VAL_NIL,
  VAL_NUMBER,
  VAL_OBJ,
  VAL_SHORT_STRING,
} ValueType;

typedef struct {
  ValueType type;
  union {
    bool   boolean;
    double number;
    Obj*   obj;
    char   shortStr[SSO_MAX + 1];
  } as;
} Value;

#define IS_BOOL(value)         ((value).type == VAL_BOOL)
#define IS_NIL(value)          ((value).type == VAL_NIL)
#define IS_NUMBER(value)       ((value).type == VAL_NUMBER)
#define IS_OBJ(value)          ((value).type == VAL_OBJ)
#define IS_SHORT_STRING(value) ((value).type == VAL_SHORT_STRING)

#define AS_BOOL(value)    ((value).as.boolean)
#define AS_NUMBER(value)  ((value).as.number)
#define AS_OBJ(value)     ((value).as.obj)
#define SSO_LENGTH(value) ((int)(uint8_t)(value).as.shortStr[SSO_MAX])
#define SSO_CHARS(value)  ((value).as.shortStr)

#define BOOL_VAL(b)   ((Value){VAL_BOOL,   {.boolean = b}})
#define NIL_VAL       ((Value){VAL_NIL,    {.number  = 0}})
#define NUMBER_VAL(n) ((Value){VAL_NUMBER, {.number  = n}})
#define OBJ_VAL(o)    ((Value){VAL_OBJ,    {.obj = (Obj*)(o)}})

static inline Value makeShortString(const char* chars, int length) {
  Value v; v.type = VAL_SHORT_STRING;
  memset(v.as.shortStr, 0, SSO_MAX + 1);
  if(length > 0) memcpy(v.as.shortStr, chars, length);
  v.as.shortStr[SSO_MAX] = (char)(uint8_t)length;
  return v;
}

/* IS_STR_VAL: true for either kind of string Value */



/* strValContents: get chars+length from any string Value; returns false if not a string */
bool strValContents(Value v, const char** chars, int* length);
bool isStrVal(Value v);

typedef struct { int capacity; int count; Value* values; } ValueArray;
void initValueArray(ValueArray* array);
void writeValueArray(ValueArray* array, Value value);
void freeValueArray(ValueArray* array);
void printValue(Value value);
bool valuesEqual(Value a, Value b);
#endif
