#ifndef clox_object_h
#define clox_object_h
#include "common.h"
#include "chunk.h"
#include "value.h"

typedef enum {
  OBJ_CLOSURE,
  OBJ_FUNCTION,
  OBJ_NATIVE,
  OBJ_STRING,
  OBJ_UPVALUE,
} ObjType;

typedef struct Obj      Obj;
typedef struct ObjString ObjString;

struct Obj { ObjType type; Obj* next; };

/* ------------------------------------------------------------------ */
/*  ObjFunction — raw compiled function (prototype)                    */
/* ------------------------------------------------------------------ */
typedef struct {
  Obj        obj;
  int        arity;
  int        upvalueCount;   /* ch25: how many upvalues this fn uses  */
  Chunk      chunk;
  ObjString* name;
} ObjFunction;

/* ------------------------------------------------------------------ */
/*  ObjUpvalue — reference to a captured variable                      */
/* ------------------------------------------------------------------ */
typedef struct ObjUpvalue {
  Obj                obj;
  Value*             location; /* points to stack slot (open) or &closed */
  Value              closed;   /* storage once variable leaves stack     */
  struct ObjUpvalue* next;     /* linked list of open upvalues           */
} ObjUpvalue;

/* ------------------------------------------------------------------ */
/*  ObjClosure — runtime closure = function + captured upvalues        */
/*                                                                     */
/*  Challenge 1: we only allocate ObjClosure when the function has    */
/*  upvalues (upvalueCount > 0).  Plain functions with no upvalues    */
/*  are called directly as ObjFunction without a closure wrapper.     */
/*  CallFrame stores a union so both paths are possible.              */
/* ------------------------------------------------------------------ */
typedef struct {
  Obj         obj;
  ObjFunction* function;
  ObjUpvalue** upvalues;
  int          upvalueCount;
} ObjClosure;

/* ------------------------------------------------------------------ */
/*  NativeResult (ch24 challenge 3)                                    */
/* ------------------------------------------------------------------ */
typedef struct { bool ok; Value value; } NativeResult;
static inline NativeResult nativeOk(Value v)   { return (NativeResult){true,  v}; }
NativeResult nativeError(const char* msg);

typedef NativeResult (*NativeFn)(int argCount, Value* args);
typedef struct {
  Obj      obj;
  int      arity;
  NativeFn function;
} ObjNative;

/* ------------------------------------------------------------------ */
/*  ObjString                                                           */
/* ------------------------------------------------------------------ */
struct ObjString { Obj obj; int length; char* chars; uint32_t hash; };

/* ------------------------------------------------------------------ */
/*  Type checks / casts                                                 */
/* ------------------------------------------------------------------ */
#define OBJ_TYPE(v)     (AS_OBJ(v)->type)
#define IS_CLOSURE(v)   isObjType(v, OBJ_CLOSURE)
#define IS_FUNCTION(v)  isObjType(v, OBJ_FUNCTION)
#define IS_NATIVE(v)    isObjType(v, OBJ_NATIVE)
#define IS_STRING(v)    isObjType(v, OBJ_STRING)
#define AS_CLOSURE(v)   ((ObjClosure*)AS_OBJ(v))
#define AS_FUNCTION(v)  ((ObjFunction*)AS_OBJ(v))
#define AS_NATIVE(v)    ((ObjNative*)AS_OBJ(v))
#define AS_STRING(v)    ((ObjString*)AS_OBJ(v))
#define AS_CSTRING(v)   (((ObjString*)AS_OBJ(v))->chars)

static inline bool isObjType(Value v, ObjType t) {
  return IS_OBJ(v) && AS_OBJ(v)->type == t;
}

ObjClosure*  newClosure (ObjFunction* function);
ObjFunction* newFunction(void);
ObjNative*   newNative  (NativeFn fn, int arity);
ObjUpvalue*  newUpvalue (Value* slot);
ObjString*   copyString (const char* chars, int length);
ObjString*   takeString (char* chars, int length);
void         printObject(Value value);
#endif
