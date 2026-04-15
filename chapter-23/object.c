#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"

/* FNV-1a hash — same algorithm as the book */
static uint32_t hashString(const char* key, int length) {
  uint32_t hash = 2166136261u;
  for (int i = 0; i < length; i++) {
    hash ^= (uint8_t)key[i];
    hash *= 16777619;
  }
  return hash;
}

static void trackObject(Obj* object, ObjType type) {
  object->type = type;
  object->next = vm.objects;
  vm.objects   = object;
}

/* Intern lookup helper */
static ObjString* findInterned(const char* chars, int length, uint32_t hash) {
  return tableFindString(&vm.strings, chars, length, hash);
}

static void internString(ObjString* s) {
  tableSet(&vm.strings, OBJ_VAL(s), NIL_VAL);
}

/* Challenge 1: copyString — FAM + owned + hash + string interning */
ObjString* copyString(const char* chars, int length) {
  uint32_t hash = hashString(chars, length);

  /* String interning: return existing copy if present */
  ObjString* interned = findInterned(chars, length, hash);
  if (interned != NULL) return interned;

  size_t totalSize = sizeof(ObjString) + length + 1;
  ObjString* string = (ObjString*)reallocate(NULL, 0, totalSize);
  trackObject(&string->obj, OBJ_STRING);
  string->length = length;
  string->owned  = true;
  string->hash   = hash;
  memcpy(string->flex, chars, length);
  string->flex[length] = '\0';
  string->chars = string->flex;

  internString(string);
  return string;
}

/* constString — delegates to copyString so the result is always
   a proper null-terminated, interned ObjString.  The "const" variant
   used to keep a raw pointer into the source buffer, but that caused
   printf to read past the end of the literal.  Copying is safer and
   the intern table still deduplicates equal strings. */
ObjString* constString(const char* chars, int length) {
  return copyString(chars, length);
}

/* concatString — owned FAM string for concatenation results */
ObjString* concatString(ObjString* a, ObjString* b) {
  int    length    = a->length + b->length;
  size_t totalSize = sizeof(ObjString) + length + 1;
  ObjString* result = (ObjString*)reallocate(NULL, 0, totalSize);
  trackObject(&result->obj, OBJ_STRING);
  result->length = length;
  result->owned  = true;
  memcpy(result->flex,             a->chars, a->length);
  memcpy(result->flex + a->length, b->chars, b->length);
  result->flex[length] = '\0';
  result->chars = result->flex;
  result->hash  = hashString(result->chars, length);

  ObjString* interned = findInterned(result->chars, length, result->hash);
  if (interned != NULL) {
    /* Already exists — free the one we just made and return the canonical copy */
    FREE_SIZE(totalSize, result);
    return interned;
  }
  internString(result);
  return result;
}

void printObject(Value value) {
  switch (OBJ_TYPE(value)) {
    case OBJ_STRING: printf("%s", AS_CSTRING(value)); break;
  }
}
