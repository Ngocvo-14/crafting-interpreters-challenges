#ifndef clox_value_h
#define clox_value_h
#include "common.h"
typedef enum { VAL_BOOL, VAL_NIL, VAL_NUMBER, VAL_OBJ } ValueType;
struct Obj;
struct ObjString;
typedef struct {
  ValueType type;
  union { bool boolean; double number; struct Obj* obj; } as;
} Value;
#define IS_BOOL(v)   ((v).type == VAL_BOOL)
#define IS_NIL(v)    ((v).type == VAL_NIL)
#define IS_NUMBER(v) ((v).type == VAL_NUMBER)
#define IS_OBJ(v)    ((v).type == VAL_OBJ)
#define AS_OBJ(v)    ((v).as.obj)
#define AS_BOOL(v)   ((v).as.boolean)
#define AS_NUMBER(v) ((v).as.number)
#define BOOL_VAL(b)  ((Value){VAL_BOOL,   {.boolean=(b)}})
#define NIL_VAL      ((Value){VAL_NIL,    {.number=0}})
#define NUMBER_VAL(n)((Value){VAL_NUMBER, {.number=(n)}})
#define OBJ_VAL(o)   ((Value){VAL_OBJ,   {.obj=(struct Obj*)(o)}})
typedef struct { int count; int capacity; Value* values; } ValueArray;
void initValueArray(ValueArray* array);
void writeValueArray(ValueArray* array, Value value);
void freeValueArray(ValueArray* array);
void printValue(Value value);
bool valuesEqual(Value a, Value b);
#endif
