#ifndef clox_vm_h
#define clox_vm_h
#include "object.h"
#include "table.h"
#include "value.h"

#define FRAMES_MAX 64
#define STACK_MAX  (FRAMES_MAX * UINT8_COUNT)

/* ------------------------------------------------------------------ */
/*  CallFrame                                                           */
/*                                                                     */
/*  Challenge 1: A frame can hold EITHER an ObjClosure* (for funcs    */
/*  that have upvalues) OR an ObjFunction* (for plain functions with  */
/*  no upvalues).  We use a tagged union so the runtime doesn't pay   */
/*  the cost of ObjClosure allocation for simple functions.           */
/* ------------------------------------------------------------------ */
typedef struct {
  bool         isClosure;   /* true → use closure field; false → use fn */
  union {
    ObjClosure*  closure;
    ObjFunction* fn;
  };
  uint8_t*     ip;
  Value*       slots;
} CallFrame;

typedef struct {
  CallFrame    frames[FRAMES_MAX];
  int          frameCount;
  Value        stack[STACK_MAX];
  Value*       stackTop;
  Table        globals;
  Table        strings;
  ObjUpvalue*  openUpvalues;   /* ch25: linked list of open upvalues  */
  Obj*         objects;
} VM;

typedef enum { INTERPRET_OK, INTERPRET_COMPILE_ERROR, INTERPRET_RUNTIME_ERROR } InterpretResult;
extern VM vm;
void            initVM(void);
void            freeVM(void);
InterpretResult interpret(const char* source);
void            push(Value value);
Value           pop(void);

/* Helper to get the ObjFunction from a CallFrame regardless of type */
static inline ObjFunction* frameFunction(CallFrame* frame) {
  return frame->isClosure ? frame->closure->function : frame->fn;
}
#endif
