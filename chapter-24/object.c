#include <stdio.h>
#include <string.h>
#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"

#define ALLOCATE_OBJ(type, objectType) \
    (type*)allocateObject(sizeof(type), objectType)

static Obj* allocateObject(size_t size, ObjType type) {
  Obj* obj = (Obj*)reallocate(NULL, 0, size);
  obj->type = type;
  obj->next = vm.objects;
  vm.objects = obj;
  return obj;
}

ObjFunction* newFunction(void) {
  ObjFunction* fn = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);
  fn->arity = 0; fn->name = NULL;
  initChunk(&fn->chunk);
  return fn;
}

/* Challenge 2: arity=-1 means variadic (skip arity check) */
ObjNative* newNative(NativeFn fn, int arity) {
  ObjNative* native = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
  native->function = fn;
  native->arity    = arity;
  return native;
}

static uint32_t hashString(const char* key, int length) {
  uint32_t hash = 2166136261u;
  for (int i=0;i<length;i++) { hash ^= (uint8_t)key[i]; hash *= 16777619; }
  return hash;
}

static ObjString* allocateString(char* chars, int length, uint32_t hash) {
  ObjString* s = ALLOCATE_OBJ(ObjString, OBJ_STRING);
  s->length = length; s->chars = chars; s->hash = hash;
  tableSet(&vm.strings, s, NIL_VAL);
  return s;
}

ObjString* takeString(char* chars, int length) {
  uint32_t hash = hashString(chars, length);
  ObjString* interned = tableFindString(&vm.strings, chars, length, hash);
  if (interned) { FREE_ARRAY(char, chars, length+1); return interned; }
  return allocateString(chars, length, hash);
}

ObjString* copyString(const char* chars, int length) {
  uint32_t hash = hashString(chars, length);
  ObjString* interned = tableFindString(&vm.strings, chars, length, hash);
  if (interned) return interned;
  char* heapChars = ALLOCATE(char, length+1);
  memcpy(heapChars, chars, length); heapChars[length]='\0';
  return allocateString(heapChars, length, hash);
}

/* Challenge 3: build a NativeResult error — needs copyString so defined here */
NativeResult nativeError(const char* msg) {
  ObjString* s = copyString(msg, (int)strlen(msg));
  return (NativeResult){false, OBJ_VAL(s)};
}

static void printFunction(ObjFunction* fn) {
  if (!fn->name) { printf("<script>"); return; }
  printf("<fn %s>", fn->name->chars);
}

void printObject(Value v) {
  switch(OBJ_TYPE(v)) {
    case OBJ_FUNCTION: printFunction(AS_FUNCTION(v)); break;
    case OBJ_NATIVE:   printf("<native fn>"); break;
    case OBJ_STRING:   printf("%s", AS_CSTRING(v)); break;
  }
}
