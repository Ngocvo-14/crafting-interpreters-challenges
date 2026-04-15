#include <stdlib.h>
#include <stdio.h>

#include "memory.h"
#include "object.h"
#include "vm.h"

void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
	  (void)oldSize;
	    if (newSize == 0) {
		        free(pointer);
			    return NULL;
			      }
	      void* result = realloc(pointer, newSize);
	        if (result == NULL) {
			    fprintf(stderr, "Out of memory.\n");
			        exit(1);
				  }
		  return result;
}

/* ------------------------------------------------------------------ */
/*  Object freeing                                                      */
/* ------------------------------------------------------------------ */

static void freeObject(Obj* object) {
	  switch (object->type) {
		      case OBJ_STRING: {
					             ObjString* s = (ObjString*)object;
						           if (!s->owned) {
								           /*
									    *          * Challenge 2: constant (non-owning) string.
									    *                   * chars points into the source buffer — do NOT free it.
									    *                            * We only free the ObjString header itself.
									    *                                     * Since constant strings use a separate allocation for the
									    *                                              * header (no FAM data), we free exactly sizeof(ObjString).
									    *                                                       */
								           FREE_SIZE(sizeof(ObjString), object);
									         } else {
											         /*
												  *          * Challenge 1: owned string with FAM.
												  *                   * The characters live inside the same allocation as the
												  *                            * ObjString header (in the flex[] member), so one free
												  *                                     * releases both.  Total size = sizeof(ObjString) + length + 1.
												  *                                              */
											         FREE_SIZE(sizeof(ObjString) + s->length + 1, object);
												       }
							         break;
								     }
				         }
}

void freeObjects(void) {
	  Obj* object = vm.objects;
	    while (object != NULL) {
		        Obj* next = object->next;
			    freeObject(object);
			        object = next;
				  }
}
