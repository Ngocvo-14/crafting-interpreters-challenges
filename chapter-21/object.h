#ifndef clox_object_h
#define clox_object_h

#include "common.h"
#include "value.h"

typedef enum { OBJ_STRING } ObjType;

struct Obj {
	  ObjType     type;
	    struct Obj* next;
};

struct ObjString {
	  struct Obj obj;
	    int        length;
	      bool       owned;
	        uint32_t   hash;
		  char*      chars;
		    char       flex[];
};

#define OBJ_TYPE(v)       (AS_OBJ(v)->type)
#define IS_STRING(v)      isObjType(v, OBJ_STRING)
#define AS_STRING(v)      ((struct ObjString*)AS_OBJ(v))
#define AS_CSTRING(v)     (((struct ObjString*)AS_OBJ(v))->chars)

static inline bool isObjType(Value value, ObjType type) {
	  return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

typedef struct ObjString ObjString;

ObjString* copyString   (const char* chars, int length);
ObjString* constString  (const char* chars, int length);
ObjString* concatString (ObjString* a, ObjString* b);
void       printObject  (Value value);

#endif
