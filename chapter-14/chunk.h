#ifndef clox_chunk_h
#define clox_chunk_h

#include "common.h"
#include "value.h"

/* ------------------------------------------------------------------ */
/*  Opcodes                                                             */
/* ------------------------------------------------------------------ */

typedef enum {
	  OP_CONSTANT,       /* 1-byte operand: index into constants (0–255)   */
	    OP_CONSTANT_LONG,  /* 3-byte operand: 24-bit index (Challenge 2)     */
	      OP_RETURN,
} OpCode;

/* ------------------------------------------------------------------ */
/*  Challenge 1: Run-length encoded line information                   */
/*                                                                      */
/*  Instead of a parallel int[] that stores one line number per byte   */
/*  of bytecode, we keep an array of (count, line) pairs.              */
/*                                                                      */
/*  Example: if bytecodes 0-4 came from line 10 and bytecodes 5-7     */
/*  came from line 11 the array contains:                              */
/*                                                                      */
/*    { {5, 10}, {3, 11} }                                             */
/*                                                                      */
/*  getLine(chunk, offset) walks the runs to decode the source line    */
/*  for any given bytecode offset.                                      */
/* ------------------------------------------------------------------ */

typedef struct {
	  int count; /* how many consecutive bytecodes share this line */
	    int line;
} LineRun;

typedef struct {
	  int      capacity;
	    int      count;
	      LineRun* runs;
} LineArray;

/* ------------------------------------------------------------------ */
/*  Chunk                                                               */
/* ------------------------------------------------------------------ */

typedef struct {
	  int        count;
	    int        capacity;
	      uint8_t*   code;
	        LineArray  lines;       /* RLE-compressed line info (Challenge 1) */
		  ValueArray constants;
} Chunk;

/* ------------------------------------------------------------------ */
/*  API                                                                 */
/* ------------------------------------------------------------------ */

void initChunk   (Chunk* chunk);
void freeChunk   (Chunk* chunk);
void writeChunk  (Chunk* chunk, uint8_t byte, int line);

/* Challenge 2: smart constant writer (picks OP_CONSTANT vs LONG). */
void writeConstant(Chunk* chunk, Value value, int line);

int  addConstant (Chunk* chunk, Value value);

/* Challenge 1: decode the source line for bytecode at offset. */
int  getLine     (Chunk* chunk, int offset);

#endif
