#ifndef clox_vm_h
#define clox_vm_h

#include "chunk.h"
#include "value.h"

/* ------------------------------------------------------------------ */
/*  Challenge 2: Dynamic stack                                          */
/*                                                                      */
/*  Instead of a fixed Value stack[STACK_MAX] array embedded in the    */
/*  struct, we heap-allocate the stack and grow it when full.          */
/*                                                                      */
/*  Costs:                                                              */
/*   - Extra heap allocation at startup and on each grow.              */
/*   - Every grow must update stackTop (it's a pointer into the        */
/*     buffer, so if realloc moves the buffer, the pointer is stale).  */
/*   - Slightly more complex initVM / freeVM.                          */
/*                                                                      */
/*  Benefits:                                                           */
/*   - No arbitrary fixed ceiling; programs with deep recursion or     */
/*     large expression trees won't silently corrupt memory.           */
/*   - Memory usage scales with actual need rather than always         */
/*     reserving STACK_MAX * sizeof(Value) bytes.                      */
/* ------------------------------------------------------------------ */

#define STACK_INITIAL 256   /* starting capacity (in Values) */

typedef struct {
	  Chunk*   chunk;
	    uint8_t* ip;

	      /* Dynamic stack fields (Challenge 2) */
	      Value*   stack;        /* heap-allocated array              */
	        Value*   stackTop;     /* points just past the top element  */
		  int      stackCapacity;/* current allocated size in Values  */
} VM;

typedef enum {
	  INTERPRET_OK,
	    INTERPRET_COMPILE_ERROR,
	      INTERPRET_RUNTIME_ERROR
} InterpretResult;

void            initVM    ();
void            freeVM    ();
InterpretResult interpret (Chunk* chunk);
void            push      (Value value);
Value           pop       ();

#endif
