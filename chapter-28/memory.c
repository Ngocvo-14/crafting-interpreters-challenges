#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "compiler.h"
#include "memory.h"
#include "vm.h"

void* reallocate(void* ptr, size_t oldSize, size_t newSize) {
  vm.bytesAllocated += newSize - oldSize;
  if (newSize > oldSize) {
#ifdef DEBUG_STRESS_GC
    collectGarbage();
#endif
    if (vm.bytesAllocated > vm.nextGC) collectGarbage();
  }
  if (newSize == 0) { free(ptr); return NULL; }
  void* result = realloc(ptr, newSize);
  if (!result) { fprintf(stderr,"Out of memory.\n"); exit(1); }
  return result;
}

static void freeObject(Obj* obj) {
  switch (OBJ_GET_TYPE(obj)) {
    case OBJ_BOUND_METHOD: FREE(ObjBoundMethod, obj); break;
    case OBJ_CLASS: {
      ObjClass* klass = (ObjClass*)obj;
      freeTable(&klass->methods);
      FREE(ObjClass, obj);
      break;
    }
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
    case OBJ_INSTANCE: {
      ObjInstance* inst = (ObjInstance*)obj;
      freeTable(&inst->fields);
      FREE(ObjInstance, obj);
      break;
    }
    case OBJ_NATIVE:  FREE(ObjNative,  obj); break;
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

void markObject(Obj* object) {
  if (object == NULL) return;
  if (OBJ_IS_MARKED(object) == vm.markBit) return;
  OBJ_SET_MARKED(object, (int)vm.markBit);
  if (vm.grayCapacity < vm.grayCount + 1) {
    vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);
    vm.grayStack = (Obj**)realloc(vm.grayStack, sizeof(Obj*)*vm.grayCapacity);
    if (!vm.grayStack) { fprintf(stderr,"GC gray stack OOM.\n"); exit(1); }
  }
  vm.grayStack[vm.grayCount++] = object;
}

void markValue(Value value) { if (IS_OBJ(value)) markObject(AS_OBJ(value)); }

static void markArray(ValueArray* array) {
  for (int i = 0; i < array->count; i++) markValue(array->values[i]);
}

static void blackenObject(Obj* object) {
  switch (OBJ_GET_TYPE(object)) {
    case OBJ_BOUND_METHOD: {
      ObjBoundMethod* bound = (ObjBoundMethod*)object;
      markValue(bound->receiver);
      markObject((Obj*)bound->method);
      break;
    }
    case OBJ_CLASS: {
      ObjClass* klass = (ObjClass*)object;
      markObject((Obj*)klass->name);
      markTable(&klass->methods);
      markObject((Obj*)klass->initMethod);  /* Challenge 1: trace cached init */
      break;
    }
    case OBJ_CLOSURE: {
      ObjClosure* closure = (ObjClosure*)object;
      markObject((Obj*)closure->function);
      for (int i = 0; i < closure->upvalueCount; i++)
        markObject((Obj*)closure->upvalues[i]);
      break;
    }
    case OBJ_FUNCTION: {
      ObjFunction* fn = (ObjFunction*)object;
      markObject((Obj*)fn->name);
      markArray(&fn->chunk.constants);
      break;
    }
    case OBJ_INSTANCE: {
      ObjInstance* inst = (ObjInstance*)object;
      markObject((Obj*)inst->klass);
      markTable(&inst->fields);
      break;
    }
    case OBJ_UPVALUE: markValue(((ObjUpvalue*)object)->closed); break;
    case OBJ_NATIVE:
    case OBJ_STRING:  break;
  }
}

static void traceReferences(void) {
  while (vm.grayCount > 0) {
    Obj* object = vm.grayStack[--vm.grayCount];
    blackenObject(object);
  }
}

static void markRoots(void) {
  for (Value* slot = vm.stack; slot < vm.stackTop; slot++) markValue(*slot);
  for (int i = 0; i < vm.frameCount; i++) {
    CallFrame* frame = &vm.frames[i];
    if (frame->isClosure) markObject((Obj*)frame->closure);
    else                  markObject((Obj*)frame->fn);
  }
  for (ObjUpvalue* uv = vm.openUpvalues; uv != NULL; uv = uv->next)
    markObject((Obj*)uv);
  markTable(&vm.globals);
  markCompilerRoots();
}

static void sweep(void) {
  Obj* previous = NULL;
  Obj* object   = vm.objects;
  while (object != NULL) {
    if (OBJ_IS_MARKED(object) == vm.markBit) {
      previous = object; object = object->next;
    } else {
      Obj* unreached = object; object = object->next;
      if (previous != NULL) previous->next = object;
      else                   vm.objects    = object;
      freeObject(unreached);
    }
  }
}

void collectGarbage(void) {
  vm.markBit ^= 1;
  markRoots();
  traceReferences();
  tableRemoveWhite(&vm.strings);
  sweep();
  vm.nextGC = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;
}
