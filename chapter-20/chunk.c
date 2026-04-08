#include <stdlib.h>

#include "chunk.h"
#include "memory.h"

/* ------------------------------------------------------------------ */
/*  LineArray helpers                                                   */
/* ------------------------------------------------------------------ */

static void initLineArray(LineArray* la) {
	  la->runs     = NULL;
	    la->count    = 0;
	      la->capacity = 0;
}

static void freeLineArray(LineArray* la) {
	  FREE_ARRAY(LineRun, la->runs, la->capacity);
	    initLineArray(la);
}

/*
 *  * Append one bytecode's line information using run-length encoding.
 *   * If the last run already records the same line number, we just
 *    * increment its count.  Otherwise we start a new run.
 *     */
static void writeLineArray(LineArray* la, int line) {
	  if (la->count > 0 && la->runs[la->count - 1].line == line) {
		      la->runs[la->count - 1].count++;
		          return;
			    }

	    /* Need a new run — grow array if necessary. */
	    if (la->capacity < la->count + 1) {
		        int old = la->capacity;
			    la->capacity = GROW_CAPACITY(old);
			        la->runs = GROW_ARRAY(LineRun, la->runs, old, la->capacity);
				  }
	      la->runs[la->count].count = 1;
	        la->runs[la->count].line  = line;
		  la->count++;
}

/* ------------------------------------------------------------------ */
/*  Challenge 1: getLine                                               */
/*                                                                      */
/*  Walk the run-length array summing counts until we reach the        */
/*  run that covers `offset`.                                           */
/* ------------------------------------------------------------------ */

int getLine(Chunk* chunk, int offset) {
	  int accumulated = 0;
	    for (int i = 0; i < chunk->lines.count; i++) {
		        accumulated += chunk->lines.runs[i].count;
			    if (offset < accumulated) {
				          return chunk->lines.runs[i].line;
					      }
			      }
	      /* Should never reach here for a valid offset. */
	      return -1;
}

/* ------------------------------------------------------------------ */
/*  Chunk lifecycle                                                     */
/* ------------------------------------------------------------------ */

void initChunk(Chunk* chunk) {
	  chunk->count    = 0;
	    chunk->capacity = 0;
	      chunk->code     = NULL;
	        initLineArray(&chunk->lines);
		  initValueArray(&chunk->constants);
}

void freeChunk(Chunk* chunk) {
	  FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
	    freeLineArray(&chunk->lines);
	      freeValueArray(&chunk->constants);
	        initChunk(chunk);
}

/* ------------------------------------------------------------------ */
/*  writeChunk                                                          */
/*                                                                      */
/*  Stores the raw byte AND records its source line in the RLE array.  */
/* ------------------------------------------------------------------ */

void writeChunk(Chunk* chunk, uint8_t byte, int line) {
	  if (chunk->capacity < chunk->count + 1) {
		      int old         = chunk->capacity;
		          chunk->capacity = GROW_CAPACITY(old);
			      chunk->code     = GROW_ARRAY(uint8_t, chunk->code, old, chunk->capacity);
			        }

	    chunk->code[chunk->count] = byte;
	      chunk->count++;

	        /* Record line info for this byte. */
	        writeLineArray(&chunk->lines, line);
}

/* ------------------------------------------------------------------ */
/*  addConstant                                                         */
/* ------------------------------------------------------------------ */

int addConstant(Chunk* chunk, Value value) {
	  writeValueArray(&chunk->constants, value);
	    return chunk->constants.count - 1;
}

/* ------------------------------------------------------------------ */
/*  Challenge 2: writeConstant                                          */
/*                                                                      */
/*  Automatically selects OP_CONSTANT  (1-byte index, max 255 consts) */
/*  or OP_CONSTANT_LONG (3-byte / 24-bit index, max 16 million consts) */
/*  based on how many constants are already in the chunk.              */
/*                                                                      */
/*  Trade-offs forced by having two instructions:                       */
/*   • The VM's dispatch loop needs two separate case arms.            */
/*   • The disassembler needs two separate decoders.                   */
/*   • Instruction length is no longer uniform: anything that scans    */
/*     the bytecode stream must consult the opcode to know how many    */
/*     bytes to skip (we already do that via disassembleInstruction,   */
/*     so this is contained).                                           */
/*   • Simplicity is mildly reduced, but the common case (< 256        */
/*     constants) remains as compact as before.                        */
/* ------------------------------------------------------------------ */

void writeConstant(Chunk* chunk, Value value, int line) {
	  int index = addConstant(chunk, value);

	    if (index <= 0xFF) {
		        /* Fits in one byte: use OP_CONSTANT. */
		        writeChunk(chunk, OP_CONSTANT, line);
			    writeChunk(chunk, (uint8_t)index, line);
			      } else {
				          /*
					   *      * Needs 24 bits: use OP_CONSTANT_LONG.
					   *           * Encode the index as three bytes, most-significant first.
					   *                */
				          writeChunk(chunk, OP_CONSTANT_LONG, line);
					      writeChunk(chunk, (uint8_t)((index >> 16) & 0xFF), line);
					          writeChunk(chunk, (uint8_t)((index >>  8) & 0xFF), line);
						      writeChunk(chunk, (uint8_t)( index        & 0xFF), line);
						        }
}
