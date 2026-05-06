#ifndef clox_chunk_h
#define clox_chunk_h
#include "common.h"
#include "value.h"
typedef enum {
  OP_CONSTANT,
  OP_NIL, OP_TRUE, OP_FALSE,
  OP_POP,
  OP_GET_LOCAL,  OP_SET_LOCAL,
  OP_GET_GLOBAL, OP_DEFINE_GLOBAL, OP_SET_GLOBAL,
  OP_GET_UPVALUE, OP_SET_UPVALUE,
  OP_CLOSE_UPVALUE,
  OP_GET_PROPERTY, OP_SET_PROPERTY,
  OP_GET_FIELD_DYNAMIC, OP_SET_FIELD_DYNAMIC,
  OP_DELETE_FIELD,
  OP_GET_SUPER,       /* ch29 */
  OP_EQUAL, OP_GREATER, OP_LESS,
  OP_ADD, OP_SUBTRACT, OP_MULTIPLY, OP_DIVIDE,
  OP_NOT, OP_NEGATE,
  OP_PRINT,
  OP_JUMP, OP_JUMP_IF_FALSE,
  OP_LOOP,
  OP_CALL,
  OP_INVOKE,
  OP_SUPER_INVOKE,    /* ch29 */
  OP_CLOSURE,
  OP_CLASS,
  OP_INHERIT,         /* ch29 */
  OP_METHOD,
  /* -------------------------------------------------------
     Challenge 3: BETA inner() dispatch
     OP_INNER argCount — at runtime walks the superclass
     chain stored on ObjClass to find the next subclass
     that defines the same method name, then calls it.
  ------------------------------------------------------- */
  OP_INNER,           /* ch29 challenge 3 */
  OP_RETURN,
} OpCode;
typedef struct { int count; int capacity; uint8_t* code; int* lines; ValueArray constants; } Chunk;
void initChunk(Chunk* chunk);
void freeChunk(Chunk* chunk);
void writeChunk(Chunk* chunk, uint8_t byte, int line);
int  addConstant(Chunk* chunk, Value value);
int  getLine(Chunk* chunk, int offset);
#endif
