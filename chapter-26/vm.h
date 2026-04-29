#ifndef clox_vm_h
#define clox_vm_h
#include "object.h"
#include "table.h"
#include "value.h"

#define FRAMES_MAX 64
#define STACK_MAX  (FRAMES_MAX * UINT8_COUNT)

typedef struct {
  bool         isClosure;
  union { ObjClosure* closure; ObjFunction* fn; };
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
  ObjUpvalue*  openUpvalues;

  /* GC bookkeeping */
  size_t       bytesAllocated;
  size_t       nextGC;

  /* Challenge 2: flip-flop mark bit — alternate between 0 and 1    */
  /* so we never need to clear isMarked during sweep. Instead, the   */
  /* "live" mark value alternates each collection cycle.             */
  uint8_t      markBit;   /* current "live" mark value: 0 or 1        */

  Obj*         objects;

  /* Gray stack for tri-color marking */
  int          grayCount;
  int          grayCapacity;
  Obj**        grayStack;
} VM;

typedef enum { INTERPRET_OK, INTERPRET_COMPILE_ERROR, INTERPRET_RUNTIME_ERROR } InterpretResult;
extern VM vm;
void            initVM(void);
void            freeVM(void);
InterpretResult interpret(const char* source);
void            push(Value value);
Value           pop(void);

static inline ObjFunction* frameFunction(CallFrame* frame) {
  return frame->isClosure ? frame->closure->function : frame->fn;
}
#endif
