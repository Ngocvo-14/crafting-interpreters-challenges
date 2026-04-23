#ifndef clox_object_h
#define clox_object_h
#include "common.h"
#include "chunk.h"
#include "value.h"

typedef enum { OBJ_FUNCTION, OBJ_NATIVE, OBJ_STRING } ObjType;

typedef struct Obj Obj;
typedef struct ObjString ObjString;

struct Obj { ObjType type; Obj* next; };

typedef struct {
  Obj obj;
  int arity;
  Chunk chunk;
  ObjString* name;
} ObjFunction;

/* ---------------------------------------------------------------
   Challenge 3: NativeResult lets native C functions signal errors
   without setjmp/longjmp.  On the happy path (.ok==true) the only
   extra cost is writing one bool — typically free on modern CPUs.
   ---------------------------------------------------------------- */
typedef struct {
  bool  ok;     /* true = success, false = runtime error */
  Value value;  /* return value (ok) or error ObjString* (error) */
} NativeResult;

static inline NativeResult nativeOk(Value v) {
  return (NativeResult){true, v};
}
NativeResult nativeError(const char* msg);

/* Challenge 2: natives carry their own arity; -1 = variadic */
typedef NativeResult (*NativeFn)(int argCount, Value* args);

typedef struct {
  Obj obj;
  int      arity;
  NativeFn function;
} ObjNative;

struct ObjString {
  Obj      obj;
  int      length;
  char*    chars;
  uint32_t hash;
};

#define OBJ_TYPE(v)    (AS_OBJ(v)->type)
#define IS_FUNCTION(v) isObjType(v, OBJ_FUNCTION)
#define IS_NATIVE(v)   isObjType(v, OBJ_NATIVE)
#define IS_STRING(v)   isObjType(v, OBJ_STRING)
#define AS_FUNCTION(v) ((ObjFunction*)AS_OBJ(v))
#define AS_NATIVE(v)   ((ObjNative*)AS_OBJ(v))
#define AS_STRING(v)   ((ObjString*)AS_OBJ(v))
#define AS_CSTRING(v)  (((ObjString*)AS_OBJ(v))->chars)

static inline bool isObjType(Value v, ObjType t) {
  return IS_OBJ(v) && AS_OBJ(v)->type == t;
}

ObjFunction* newFunction(void);
ObjNative*   newNative(NativeFn fn, int arity);
ObjString*   copyString(const char* chars, int length);
ObjString*   takeString(char* chars, int length);
void         printObject(Value value);
#endif
