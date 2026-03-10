#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "debug.h"
#include "vm.h"

VM vm;

/* ------------------------------------------------------------------ */
/*  Challenge 2: dynamic stack helpers                                  */
/* ------------------------------------------------------------------ */

static void resetStack() {
	  vm.stackTop = vm.stack;   /* point to beginning = empty stack */
}

/*
 *  * Grow the stack by 2× when it is full.
 *   *
 *    * The tricky part: stackTop is a raw pointer into the old buffer.
 *     * After realloc the buffer may move, so we must recalculate stackTop
 *      * from the saved integer offset before the pointer becomes stale.
 *       */
static void growStack() {
	  int    oldCap    = vm.stackCapacity;
	    int    newCap    = oldCap * 2;
	      /* Save offset BEFORE realloc moves the buffer. */
	      int    topOffset = (int)(vm.stackTop - vm.stack);

	        vm.stack         = (Value*)realloc(vm.stack, newCap * sizeof(Value));
		  if (vm.stack == NULL) {
			      fprintf(stderr, "Stack overflow: could not grow stack.\n");
			          exit(1);
				    }
		    vm.stackCapacity = newCap;
		      /* Restore pointer into (possibly new) buffer. */
		      vm.stackTop      = vm.stack + topOffset;
}

/* ------------------------------------------------------------------ */
/*  VM lifecycle                                                        */
/* ------------------------------------------------------------------ */

void initVM() {
	  vm.stackCapacity = STACK_INITIAL;
	    vm.stack         = (Value*)malloc(vm.stackCapacity * sizeof(Value));
	      if (vm.stack == NULL) {
		          fprintf(stderr, "Could not allocate VM stack.\n");
			      exit(1);
			        }
	        resetStack();
}

void freeVM() {
	  free(vm.stack);
	    vm.stack         = NULL;
	      vm.stackTop      = NULL;
	        vm.stackCapacity = 0;
}

/* ------------------------------------------------------------------ */
/*  Stack operations                                                    */
/* ------------------------------------------------------------------ */

void push(Value value) {
	  /* Challenge 2: grow before overflow. */
	  if (vm.stackTop - vm.stack >= vm.stackCapacity) {
		      growStack();
		        }
	    *vm.stackTop = value;
	      vm.stackTop++;
}

Value pop() {
	  vm.stackTop--;
	    return *vm.stackTop;
}

/* ------------------------------------------------------------------ */
/*  run() — the interpreter loop                                        */
/* ------------------------------------------------------------------ */

static InterpretResult run() {
#define READ_BYTE()     (*vm.ip++)
#define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])

	/*
	 *  * Challenge 3: in-place negation macro.
	 *   *
	 *    * Original:  push(-pop())
	 *     *   → decrements stackTop, reads value, negates, then increments
	 *      *     stackTop again.  Two pointer writes that cancel out.
	 *       *
	 *        * Optimised: peek at the top slot directly and negate in place.
	 *         *   → zero pointer movement; one array write.
	 *          *
	 *           * The same optimisation applies to any unary instruction that
	 *            * consumes exactly one value and produces one value of the same
	 *             * size (e.g. a future OP_NOT for booleans, or OP_BITWISE_NOT).
	 *              * It does NOT apply to binary operators because those genuinely
	 *               * reduce the stack height by one.
	 *                */
#define NEGATE_IN_PLACE() \
	    do { *(vm.stackTop - 1) = -(*(vm.stackTop - 1)); } while (false)

#define BINARY_OP(op) \
	    do { \
		          double b = pop(); \
		          double a = pop(); \
		          push(a op b); \
		        } while (false)

	  for (;;) {

#ifdef DEBUG_TRACE_EXECUTION
		      printf("          ");
		          for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
				        printf("[ ");
					      printValue(*slot);
					            printf(" ]");
						        }
			      printf("\n");
			          disassembleInstruction(vm.chunk, (int)(vm.ip - vm.chunk->code));
#endif

				      uint8_t instruction;
				          switch (instruction = READ_BYTE()) {
						        case OP_CONSTANT: {
										          Value constant = READ_CONSTANT();
											          push(constant);
												          break;
													        }
									        case OP_CONSTANT_LONG: {
													               /* 3-byte operand, big-endian (from Chapter 14 Challenge 2) */
													               uint32_t hi  = READ_BYTE();
														               uint32_t mid = READ_BYTE();
															               uint32_t lo  = READ_BYTE();
																               uint32_t idx = (hi << 16) | (mid << 8) | lo;
																	               push(vm.chunk->constants.values[idx]);
																		               break;
																			             }

												             /* Challenge 3: OP_NEGATE uses in-place optimisation. */
												             case OP_NEGATE:
												               NEGATE_IN_PLACE();
													               break;

														             case OP_ADD:      BINARY_OP(+); break;
																	             case OP_SUBTRACT: BINARY_OP(-); break;
																				             case OP_MULTIPLY: BINARY_OP(*); break;
																							             case OP_DIVIDE:   BINARY_OP(/); break;

																										             case OP_RETURN: {
																														             printValue(pop());
																															             printf("\n");
																																             return INTERPRET_OK;
																																	           }
																										           }
					    }

#undef READ_BYTE
#undef READ_CONSTANT
#undef NEGATE_IN_PLACE
#undef BINARY_OP
}

/* ------------------------------------------------------------------ */
/*  interpret()                                                         */
/* ------------------------------------------------------------------ */

InterpretResult interpret(Chunk* chunk) {
	  vm.chunk = chunk;
	    vm.ip    = vm.chunk->code;
	      return run();
}
