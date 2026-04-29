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

/* ================================================================== */
/*  Challenge 1: Compact Obj header                                    */
/*                                                                     */
/*  Baseline layout (64-bit machine):                                  */
/*    ObjType type    → 4 bytes (enum = int)                          */
/*    bool isMarked   → 1 byte  + 3 bytes padding                     */
/*    Obj*  next      → 8 bytes                                        */
/*    Total           → 16 bytes                                       */
/*                                                                     */
/*  Compact layout: pack type and isMarked into a single uint32_t.    */
/*  We use the low 8 bits for the type tag and bit 8 for isMarked.    */
/*  next stays as a pointer.                                           */
/*    uint32_t typeAndMark → 4 bytes                                   */
/*    Obj*     next        → 8 bytes (+ 4 bytes padding before it)    */
/*    Total                → 16 bytes                                  */
/*                                                                     */
/*  On most 64-bit ABI (System V), the pointer must be 8-byte         */
/*  aligned, so we still get 4 bytes of padding after typeAndMark.    */
/*  The struct is the same size BUT we save one field access — type   */
/*  and isMarked are in the same cache word, so checking both is a    */
/*  single 32-bit load.                                                */
/*                                                                     */
/*  Runtime cost: a mask + shift on every type/mark read. Modern CPUs */
/*  handle this in one cycle. The trade-off is slightly more complex  */
/*  macro code for zero extra memory saved on 64-bit platforms, but   */
/*  on 32-bit platforms (where pointers are 4 bytes) the struct drops */
/*  from 12 to 8 bytes — a 33% reduction.                             */
/* ================================================================== */
struct Obj {
  uint32_t typeAndMark; /* low 8 bits = ObjType, bit 8 = isMarked     */
  Obj*     next;
};

/* Accessor macros for the packed fields */
#define OBJ_GET_TYPE(obj)      ((ObjType)((obj)->typeAndMark & 0xFF))
#define OBJ_IS_MARKED(obj)     (((obj)->typeAndMark >> 8) & 1)
#define OBJ_SET_TYPE(obj, t)   ((obj)->typeAndMark = ((obj)->typeAndMark & ~0xFF) | (uint8_t)(t))
#define OBJ_SET_MARKED(obj, m) ((obj)->typeAndMark = ((obj)->typeAndMark & ~0x100) | ((m)?0x100:0))
#define OBJ_INIT(obj, t)       ((obj)->typeAndMark = (uint32_t)(t))

typedef struct {
  Obj        obj;
  int        arity;
  int        upvalueCount;
  Chunk      chunk;
  ObjString* name;
} ObjFunction;

typedef struct ObjUpvalue {
  Obj                obj;
  Value*             location;
  Value              closed;
  struct ObjUpvalue* next;
} ObjUpvalue;

typedef struct {
  Obj          obj;
  ObjFunction* function;
  ObjUpvalue** upvalues;
  int          upvalueCount;
} ObjClosure;

typedef struct { bool ok; Value value; } NativeResult;
static inline NativeResult nativeOk(Value v) { return (NativeResult){true, v}; }
NativeResult nativeError(const char* msg);

typedef NativeResult (*NativeFn)(int argCount, Value* args);
typedef struct {
  Obj      obj;
  int      arity;
  NativeFn function;
} ObjNative;

struct ObjString {
  Obj      obj;
  int      length;
  char*    chars;
  uint32_t hash;
};

/* Type checks use the packed accessor */
#define OBJ_TYPE(v)     OBJ_GET_TYPE(AS_OBJ(v))
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
  return IS_OBJ(v) && OBJ_GET_TYPE(AS_OBJ(v)) == t;
}

ObjClosure*  newClosure (ObjFunction* function);
ObjFunction* newFunction(void);
ObjNative*   newNative  (NativeFn fn, int arity);
ObjUpvalue*  newUpvalue (Value* slot);
ObjString*   copyString (const char* chars, int length);
ObjString*   takeString (char* chars, int length);
void         printObject(Value value);
#endif
