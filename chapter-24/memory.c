#include <stdio.h>
#include <stdlib.h>
#include "memory.h"
#include "vm.h"

void* reallocate(void* ptr, size_t oldSize, size_t newSize) {
  (void)oldSize;
  if (newSize == 0) { free(ptr); return NULL; }
  void* result = realloc(ptr, newSize);
  if (!result) { fprintf(stderr, "Out of memory.\n"); exit(1); }
  return result;
}

static void freeObject(Obj* obj) {
  switch (obj->type) {
    case OBJ_FUNCTION: {
      ObjFunction* fn = (ObjFunction*)obj;
      freeChunk(&fn->chunk);
      FREE(ObjFunction, obj);
      break;
    }
    case OBJ_NATIVE:
      FREE(ObjNative, obj);
      break;
    case OBJ_STRING: {
      ObjString* s = (ObjString*)obj;
      FREE_ARRAY(char, s->chars, s->length + 1);
      FREE(ObjString, obj);
      break;
    }
  }
}

void freeObjects(void) {
  Obj* obj = vm.objects;
  while (obj) { Obj* next = obj->next; freeObject(obj); obj = next; }
}
