#ifndef clox_object_h
#define clox_object_h
#include "common.h"
#include "chunk.h"
#include "table.h"
#include "value.h"

typedef enum {
  OBJ_CLASS,
  OBJ_CLOSURE,
  OBJ_FUNCTION,
  OBJ_INSTANCE,
  OBJ_NATIVE,
  OBJ_STRING,
  OBJ_UPVALUE,
} ObjType;

typedef struct Obj      Obj;
typedef struct ObjString ObjString;

struct Obj {
  uint32_t typeAndMark;
  Obj*     next;
};

#define OBJ_GET_TYPE(obj)      ((ObjType)((obj)->typeAndMark & 0xFF))
#define OBJ_IS_MARKED(obj)     (((obj)->typeAndMark >> 8) & 1)
#define OBJ_SET_TYPE(obj, t)   ((obj)->typeAndMark = ((obj)->typeAndMark & ~0xFF) | (uint8_t)(t))
#define OBJ_SET_MARKED(obj, m) ((obj)->typeAndMark = ((obj)->typeAndMark & ~0x100) | ((m)?0x100:0))
#define OBJ_INIT(obj, t)       ((obj)->typeAndMark = (uint32_t)(t))

/* ------------------------------------------------------------------ */
/*  ObjFunction                                                         */
/* ------------------------------------------------------------------ */
typedef struct {
  Obj        obj;
  int        arity;
  int        upvalueCount;
  Chunk      chunk;
  ObjString* name;
} ObjFunction;

/* ------------------------------------------------------------------ */
/*  ObjUpvalue                                                          */
/* ------------------------------------------------------------------ */
typedef struct ObjUpvalue {
  Obj                obj;
  Value*             location;
  Value              closed;
  struct ObjUpvalue* next;
} ObjUpvalue;

/* ------------------------------------------------------------------ */
/*  ObjClosure                                                          */
/* ------------------------------------------------------------------ */
typedef struct {
  Obj          obj;
  ObjFunction* function;
  ObjUpvalue** upvalues;
  int          upvalueCount;
} ObjClosure;

/* ------------------------------------------------------------------ */
/*  ObjClass                                                            */
/* ------------------------------------------------------------------ */
typedef struct {
  Obj        obj;
  ObjString* name;
} ObjClass;

/* ------------------------------------------------------------------ */
/*  ObjInstance                                                         */
/* ------------------------------------------------------------------ */
typedef struct {
  Obj       obj;
  ObjClass* klass;
  Table     fields;
} ObjInstance;

/* ------------------------------------------------------------------ */
/*  NativeResult / ObjNative                                            */
/* ------------------------------------------------------------------ */
typedef struct { bool ok; Value value; } NativeResult;
static inline NativeResult nativeOk(Value v) { return (NativeResult){true, v}; }
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
#define OBJ_TYPE(v)      OBJ_GET_TYPE(AS_OBJ(v))
#define IS_CLASS(v)      isObjType(v, OBJ_CLASS)
#define IS_CLOSURE(v)    isObjType(v, OBJ_CLOSURE)
#define IS_FUNCTION(v)   isObjType(v, OBJ_FUNCTION)
#define IS_INSTANCE(v)   isObjType(v, OBJ_INSTANCE)
#define IS_NATIVE(v)     isObjType(v, OBJ_NATIVE)
#define IS_STRING(v)     isObjType(v, OBJ_STRING)
#define AS_CLASS(v)      ((ObjClass*)AS_OBJ(v))
#define AS_CLOSURE(v)    ((ObjClosure*)AS_OBJ(v))
#define AS_FUNCTION(v)   ((ObjFunction*)AS_OBJ(v))
#define AS_INSTANCE(v)   ((ObjInstance*)AS_OBJ(v))
#define AS_NATIVE(v)     ((ObjNative*)AS_OBJ(v))
#define AS_STRING(v)     ((ObjString*)AS_OBJ(v))
#define AS_CSTRING(v)    (((ObjString*)AS_OBJ(v))->chars)

static inline bool isObjType(Value v, ObjType t) {
  return IS_OBJ(v) && OBJ_GET_TYPE(AS_OBJ(v)) == t;
}

ObjClass*    newClass   (ObjString* name);
ObjClosure*  newClosure (ObjFunction* function);
ObjFunction* newFunction(void);
ObjInstance* newInstance(ObjClass* klass);
ObjNative*   newNative  (NativeFn fn, int arity);
ObjUpvalue*  newUpvalue (Value* slot);
ObjString*   copyString (const char* chars, int length);
ObjString*   takeString (char* chars, int length);
void         printObject(Value value);
#endif
