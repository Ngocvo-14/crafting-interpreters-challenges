#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler.h"
#include "memory.h"
#include "vm.h"

/* ================================================================== */
/*  reallocate — central allocation hook                               */
/* ================================================================== */
void* reallocate(void* ptr, size_t oldSize, size_t newSize) {
  vm.bytesAllocated += newSize - oldSize;

  if (newSize > oldSize) {
#ifdef DEBUG_STRESS_GC
    collectGarbage();
#endif
    if (vm.bytesAllocated > vm.nextGC)
      collectGarbage();
  }

  if (newSize == 0) { free(ptr); return NULL; }
  void* result = realloc(ptr, newSize);
  if (!result) { fprintf(stderr, "Out of memory.\n"); exit(1); }
  return result;
}

/* ================================================================== */
/*  freeObject                                                         */
/* ================================================================== */
static void freeObject(Obj* obj) {
  switch (OBJ_GET_TYPE(obj)) {
    case OBJ_CLOSURE: {
      ObjClosure* c = (ObjClosure*)obj;
      FREE_ARRAY(ObjUpvalue*, c->upvalues, c->upvalueCount);
      FREE(ObjClosure, obj);
      break;
    }
    case OBJ_FUNCTION: {
      ObjFunction* fn = (ObjFunction*)obj;
      freeChunk(&fn->chunk);
      FREE(ObjFunction, obj);
      break;
    }
    case OBJ_NATIVE:   FREE(ObjNative,   obj); break;
    case OBJ_STRING: {
      ObjString* s = (ObjString*)obj;
      FREE_ARRAY(char, s->chars, s->length+1);
      FREE(ObjString, obj);
      break;
    }
    case OBJ_UPVALUE: FREE(ObjUpvalue, obj); break;
  }
}

void freeObjects(void) {
  Obj* obj = vm.objects;
  while (obj) { Obj* next = obj->next; freeObject(obj); obj = next; }
  free(vm.grayStack);
}

/* ================================================================== */
/*  Marking                                                            */
/* ================================================================== */

void markObject(Obj* object) {
  if (object == NULL) return;
  /* Challenge 2: compare against vm.markBit instead of a fixed true */
  if (OBJ_IS_MARKED(object) == vm.markBit) return; /* already marked */
  OBJ_SET_MARKED(object, (int)vm.markBit);

  /* Add to gray stack */
  if (vm.grayCapacity < vm.grayCount + 1) {
    vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);
    vm.grayStack = (Obj**)realloc(vm.grayStack,
                                   sizeof(Obj*) * vm.grayCapacity);
    if (!vm.grayStack) { fprintf(stderr, "GC gray stack OOM.\n"); exit(1); }
  }
  vm.grayStack[vm.grayCount++] = object;
}

void markValue(Value value) {
  if (IS_OBJ(value)) markObject(AS_OBJ(value));
}

static void markArray(ValueArray* array) {
  for (int i = 0; i < array->count; i++) markValue(array->values[i]);
}

/* ================================================================== */
/*  Blackening (tracing references)                                    */
/* ================================================================== */

static void blackenObject(Obj* object) {
  switch (OBJ_GET_TYPE(object)) {
    case OBJ_CLOSURE: {
      ObjClosure* closure = (ObjClosure*)object;
      markObject((Obj*)closure->function);
      for (int i = 0; i < closure->upvalueCount; i++)
        markObject((Obj*)closure->upvalues[i]);
      break;
    }
    case OBJ_FUNCTION: {
      ObjFunction* function = (ObjFunction*)object;
      markObject((Obj*)function->name);
      markArray(&function->chunk.constants);
      break;
    }
    case OBJ_UPVALUE:
      markValue(((ObjUpvalue*)object)->closed);
      break;
    case OBJ_NATIVE:
    case OBJ_STRING:
      break; /* no outgoing references */
  }
}

static void traceReferences(void) {
  while (vm.grayCount > 0) {
    Obj* object = vm.grayStack[--vm.grayCount];
    blackenObject(object);
  }
}

/* ================================================================== */
/*  Marking roots                                                      */
/* ================================================================== */

static void markRoots(void) {
  /* Value stack */
  for (Value* slot = vm.stack; slot < vm.stackTop; slot++)
    markValue(*slot);

  /* Call frames — closures and plain functions */
  for (int i = 0; i < vm.frameCount; i++) {
    CallFrame* frame = &vm.frames[i];
    if (frame->isClosure)
      markObject((Obj*)frame->closure);
    else
      markObject((Obj*)frame->fn);
  }

  /* Open upvalues */
  for (ObjUpvalue* uv = vm.openUpvalues; uv != NULL; uv = uv->next)
    markObject((Obj*)uv);

  /* Globals */
  markTable(&vm.globals);

  /* Compiler roots */
  markCompilerRoots();
}

/* ================================================================== */
/*  Sweep                                                              */
/*                                                                     */
/*  Challenge 2: flip-flop mark bit                                   */
/*                                                                     */
/*  Instead of clearing isMarked on every live object during sweep    */
/*  (O(live) writes), we flip vm.markBit at the START of each cycle.  */
/*  Objects marked in the previous cycle have the OLD markBit value.  */
/*  During marking we set them to the NEW markBit.                    */
/*  In sweep, a "live" object is one whose mark matches the CURRENT   */
/*  vm.markBit. An object with the old value is dead and can be freed.*/
/*                                                                     */
/*  This eliminates ALL clearing writes during sweep — we only write  */
/*  to objects we're actually going to free anyway. The win is most   */
/*  visible when the heap has many long-lived objects: a program with  */
/*  10,000 live objects and 100 dead ones saves 10,000 store          */
/*  instructions per GC cycle.                                        */
/* ================================================================== */

static void sweep(void) {
  Obj* previous = NULL;
  Obj* object   = vm.objects;
  while (object != NULL) {
    /* Live = marked with CURRENT markBit */
    if (OBJ_IS_MARKED(object) == vm.markBit) {
      previous = object;
      object   = object->next;
    } else {
      /* Dead — unlink and free */
      Obj* unreached = object;
      object = object->next;
      if (previous != NULL) previous->next = object;
      else                   vm.objects    = object;
      freeObject(unreached);
    }
  }
}

/* ================================================================== */
/*  collectGarbage                                                     */
/* ================================================================== */

void collectGarbage(void) {
#ifdef DEBUG_LOG_GC
  printf("-- gc begin (markBit=%d, bytes=%zu)\n", vm.markBit, vm.bytesAllocated);
  size_t before = vm.bytesAllocated;
#endif

  /* Challenge 2: flip the mark bit before marking so new marks use
     the NEW value and old marks are considered "dead" */
  vm.markBit ^= 1;

  markRoots();
  traceReferences();
  tableRemoveWhite(&vm.strings);
  sweep();

  vm.nextGC = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;

#ifdef DEBUG_LOG_GC
  printf("-- gc end: collected %zu bytes (from %zu to %zu) next at %zu\n",
         before - vm.bytesAllocated, before, vm.bytesAllocated, vm.nextGC);
#endif
}
