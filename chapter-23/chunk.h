#ifndef clox_chunk_h
#define clox_chunk_h
#include "common.h"
#include "value.h"

typedef enum {
  OP_CONSTANT, OP_CONSTANT_LONG,
  OP_NIL, OP_TRUE, OP_FALSE, OP_POP,
  OP_GET_LOCAL, OP_SET_LOCAL,
  OP_GET_LOCAL_WIDE, OP_SET_LOCAL_WIDE,
  OP_GET_GLOBAL, OP_DEFINE_GLOBAL, OP_SET_GLOBAL,
  OP_EQUAL, OP_GREATER, OP_LESS,
  OP_ADD, OP_SUBTRACT, OP_MULTIPLY, OP_DIVIDE,
  OP_NOT, OP_NEGATE,
  OP_PRINT,
  /* ch23 control flow */
  OP_JUMP,            /* unconditional forward jump; 2-byte offset */
  OP_JUMP_IF_FALSE,   /* jump forward if falsey; 2-byte offset     */
  OP_LOOP,            /* unconditional backward jump; 2-byte offset */
  /* ch23 challenge 1: switch */
  OP_SWITCH_EQUAL,    /* peek(1)==peek(0) ? pop both and skip next jump : pop top */
  /* ch23 challenge 3: repeat/until */
  /* (uses OP_LOOP + OP_JUMP_IF_FALSE — no new opcode needed) */
  OP_RETURN,
} OpCode;

typedef struct { int count; int line; } LineRun;
typedef struct { int capacity; int count; LineRun* runs; } LineArray;
typedef struct {
  int count; int capacity;
  uint8_t* code;
  LineArray lines;
  ValueArray constants;
} Chunk;

void initChunk    (Chunk* chunk);
void freeChunk    (Chunk* chunk);
void writeChunk   (Chunk* chunk, uint8_t byte, int line);
void writeConstant(Chunk* chunk, Value value, int line);
int  addConstant  (Chunk* chunk, Value value);
int  getLine      (Chunk* chunk, int offset);
#endif
