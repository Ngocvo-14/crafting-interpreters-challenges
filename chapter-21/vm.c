#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "common.h"
#include "chunk.h"
#include "compiler.h"
#include "debug.h"
#include "object.h"
#include "memory.h"
#include "table.h"
#include "vm.h"

VM vm;

static void resetStack() { vm.stackTop = vm.stack; }

static void growStack() {
	  int oldCap = vm.stackCapacity, newCap = oldCap * 2;
	    int topOff = (int)(vm.stackTop - vm.stack);
	      vm.stack = (Value*)realloc(vm.stack, newCap * sizeof(Value));
	        if (!vm.stack) { fprintf(stderr, "Stack overflow.\n"); exit(1); }
		  vm.stackCapacity = newCap;
		    vm.stackTop = vm.stack + topOff;
}

void initVM(void) {
	  vm.stackCapacity = STACK_INITIAL;
	    vm.stack = (Value*)malloc(vm.stackCapacity * sizeof(Value));
	      if (!vm.stack) { fprintf(stderr, "Could not allocate stack.\n"); exit(1); }
	        resetStack();
		  vm.objects = NULL;
		    initTable(&vm.globals);
		      initTable(&vm.strings);
}

void freeVM(void) {
	  freeTable(&vm.globals);
	    freeTable(&vm.strings);
	      freeObjects();
	        free(vm.stack);
		  vm.stack = NULL;
}

void push(Value value) {
	  if (vm.stackTop - vm.stack >= vm.stackCapacity) growStack();
	    *vm.stackTop++ = value;
}
Value pop(void) { return *--vm.stackTop; }
static Value peek(int distance) { return vm.stackTop[-1 - distance]; }

static void runtimeError(const char* format, ...) {
	  va_list args;
	    va_start(args, format);
	      vfprintf(stderr, format, args);
	        va_end(args);
		  fputs("\n", stderr);
		    size_t instruction = vm.ip - vm.chunk->code - 1;
		      fprintf(stderr, "[line %d] in script\n",
				                getLine(vm.chunk, (int)instruction));
		        resetStack();
}

static bool isFalsey(Value value) {
	  return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static ObjString* valueToString(Value value) {
	  char buf[64];
	    if (IS_NUMBER(value)) {
		        int len = snprintf(buf, sizeof(buf), "%g", AS_NUMBER(value));
			    return copyString(buf, len);
			      } else if (IS_BOOL(value)) {
				          const char* s = AS_BOOL(value) ? "true" : "false";
					      return copyString(s, (int)strlen(s));
					        } else if (IS_NIL(value)) {
							    return copyString("nil", 3);
							      }
	      return copyString("?", 1);
}

static InterpretResult run(void) {
#define READ_BYTE()     (*vm.ip++)
#define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])
#define READ_STRING()   AS_STRING(READ_CONSTANT())
#define BINARY_OP(valueType, op) \
	  do { \
		      if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
			            runtimeError("Operands must be numbers."); \
			            return INTERPRET_RUNTIME_ERROR; \
			          } \
		      double b = AS_NUMBER(pop()), a = AS_NUMBER(pop()); \
		      push(valueType(a op b)); \
		    } while(false)

	  for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
		      printf("          ");
		          for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
				        printf("[ "); printValue(*slot); printf(" ]");
					    }
			      printf("\n");
			          disassembleInstruction(vm.chunk, (int)(vm.ip - vm.chunk->code));
#endif
				      uint8_t instruction;
				          switch (instruction = READ_BYTE()) {
						        case OP_CONSTANT: { push(READ_CONSTANT()); break; }
									        case OP_CONSTANT_LONG: {
													               uint32_t hi=READ_BYTE(), mid=READ_BYTE(), lo=READ_BYTE();
														               push(vm.chunk->constants.values[(hi<<16)|(mid<<8)|lo]); break;
															             }
									        case OP_NIL:    push(NIL_VAL);          break;
												      case OP_TRUE:   push(BOOL_VAL(true));   break;
														            case OP_FALSE:  push(BOOL_VAL(false));  break;
																	          case OP_POP:    pop();                  break;

																				        case OP_GET_GLOBAL: {
																								            ObjString* name = READ_STRING();
																									            Value value;
																										            if (!tableGet(&vm.globals, OBJ_VAL(name), &value)) {
																												              runtimeError("Undefined variable '%s'.", name->chars);
																													                return INTERPRET_RUNTIME_ERROR;
																															        }
																											            push(value);
																												            break;
																													          }
																				        case OP_DEFINE_GLOBAL: {
																								               ObjString* name = READ_STRING();
																									               tableSet(&vm.globals, OBJ_VAL(name), peek(0));
																										               pop();
																											               break;
																												             }
																							             case OP_SET_GLOBAL: {
																												         ObjString* name = READ_STRING();
																													         Value dummy;
																														         if (!tableGet(&vm.globals, OBJ_VAL(name), &dummy)) {
																																           runtimeError("Undefined variable '%s'.", name->chars);
																																	             return INTERPRET_RUNTIME_ERROR;
																																		             }
																															         tableSet(&vm.globals, OBJ_VAL(name), peek(0));
																																         break;
																																	       }

																											       case OP_EQUAL: {
																														              Value b = pop(), a = pop();
																															              push(BOOL_VAL(valuesEqual(a, b))); break;
																																            }
																													            case OP_GREATER:  BINARY_OP(BOOL_VAL, >);   break;
																																            case OP_LESS:     BINARY_OP(BOOL_VAL, <);   break;
																																			            case OP_ADD: {
																																							         if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
																																									           ObjString* b = AS_STRING(pop());
																																										             ObjString* a = AS_STRING(pop());
																																											               push(OBJ_VAL(concatString(a, b)));
																																												               } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
																																														                 double b = AS_NUMBER(pop()), a = AS_NUMBER(pop());
																																																           push(NUMBER_VAL(a + b));
																																																	           } else if (IS_STRING(peek(0)) || IS_STRING(peek(1))) {
																																																			             Value right = pop(), left = pop();
																																																				               ObjString* a = IS_STRING(left)  ? AS_STRING(left)  : valueToString(left);
																																																					                 ObjString* b = IS_STRING(right) ? AS_STRING(right) : valueToString(right);
																																																							           push(OBJ_VAL(concatString(a, b)));
																																																								           } else {
																																																										             runtimeError("Operands must be two numbers or two strings.");
																																																											               return INTERPRET_RUNTIME_ERROR;
																																																												               }
																																								         break;
																																									       }
																																			            case OP_SUBTRACT: BINARY_OP(NUMBER_VAL, -); break;
																																						            case OP_MULTIPLY: BINARY_OP(NUMBER_VAL, *); break;
																																									            case OP_DIVIDE:   BINARY_OP(NUMBER_VAL, /); break;
																																												            case OP_NOT:      push(BOOL_VAL(isFalsey(pop()))); break;
																																															            case OP_NEGATE:
																																															              if (!IS_NUMBER(peek(0))) {
																																																	                runtimeError("Operand must be a number.");
																																																			          return INTERPRET_RUNTIME_ERROR;
																																																				          }
																																																              *(vm.stackTop - 1) = NUMBER_VAL(-AS_NUMBER(*(vm.stackTop - 1)));
																																																	              break;
																																																		            case OP_PRINT:
																																																		              printValue(pop());
																																																			              printf("\n");
																																																				              break;
																																																					            case OP_RETURN:
																																																					              return INTERPRET_OK;
																																																						          }
					    }
#undef READ_BYTE
#undef READ_CONSTANT
#undef READ_STRING
#undef BINARY_OP
}

InterpretResult interpret(const char* source) {
	  Chunk chunk;
	    initChunk(&chunk);
	      if (!compile(source, &chunk)) { freeChunk(&chunk); return INTERPRET_COMPILE_ERROR; }
	        vm.chunk = &chunk;
		  vm.ip    = vm.chunk->code;
		    InterpretResult result = run();
		      freeChunk(&chunk);
		        return result;
}
