#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "value.h"
#include "vm.h"

/* ------------------------------------------------------------------ */
/*  Internal: register object with VM's linked list                    */
/* ------------------------------------------------------------------ */

static void trackObject(Obj* object, ObjType type) {
	  object->type = type;
	    object->next = vm.objects;
	      vm.objects   = object;
}

/* ------------------------------------------------------------------ */
/*  Challenge 1: copyString — single contiguous allocation via FAM    */
/*                                                                      */
/*  We allocate:  sizeof(ObjString) + length + 1  bytes in one shot.  */
/*  The ObjString header occupies the first sizeof(ObjString) bytes.   */
/*  The flexible array member `flex[]` follows immediately after —     */
/*  we copy the characters there and point `chars` at it.              */
/* ------------------------------------------------------------------ */

ObjString* copyString(const char* chars, int length) {
	  /* One allocation: header + characters + null terminator */
	  size_t totalSize = sizeof(ObjString) + length + 1;
	    ObjString* string = (ObjString*)reallocate(NULL, 0, totalSize);

	      trackObject(&string->obj, OBJ_STRING);
	        string->length = length;
		  string->owned  = true;          /* Challenge 2: this string owns its chars */

		    /* Copy characters into the flexible array member */
		    memcpy(string->flex, chars, length);
		      string->flex[length] = '\0';

		        /* Point chars at the FAM data — same object, zero extra allocation */
		        string->chars = string->flex;

			  return string;
}

/* ------------------------------------------------------------------ */
/*  Challenge 2: constString — non-owning pointer into existing buffer */
/*                                                                      */
/*  No character copy.  We allocate only the ObjString header and      */
/*  point chars directly at the caller's buffer.  `owned` = false      */
/*  tells freeObject() not to free chars.                              */
/*                                                                      */
/*  Safe to use for string literals because the source buffer is kept  */
/*  alive for the entire duration of interpret().                       */
/* ------------------------------------------------------------------ */

ObjString* constString(const char* chars, int length) {
	  /* Only the header — no FAM data needed */
	  ObjString* string = (ObjString*)reallocate(NULL, 0, sizeof(ObjString));

	    trackObject(&string->obj, OBJ_STRING);
	      string->length = length;
	        string->owned  = false;         /* Challenge 2: does NOT own chars */
		  string->chars  = (char*)chars;  /* points into caller's buffer    */
		    /* flex[] is empty — zero extra bytes were allocated               */

		    return string;
}

/* ------------------------------------------------------------------ */
/*  concatString — allocate owned string for concatenation result     */
/* ------------------------------------------------------------------ */

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

			      return result;
}

/* ------------------------------------------------------------------ */
/*  printObject                                                         */
/* ------------------------------------------------------------------ */

void printObject(Value value) {
	  switch (OBJ_TYPE(value)) {
		      case OBJ_STRING:
			            printf("%s", AS_CSTRING(value));
				          break;
					    }
}
