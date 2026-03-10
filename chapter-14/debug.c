#include <stdio.h>

#include "debug.h"
#include "value.h"

//Internal helpers
static int simpleInstruction(const char* name, int offset) {
	  printf("%s\n", name);
	    return offset + 1;
}

static int constantInstruction(const char* name, Chunk* chunk, int offset) {
	  uint8_t idx = chunk->code[offset + 1];
	    printf("%-20s %4d '", name, idx);
	      printValue(chunk->constants.values[idx]);
	        printf("'\n");
		  return offset + 2;   /* opcode + 1-byte operand */
}

/*
 *  * Challenge 2: disassemble OP_CONSTANT_LONG.
 *   * Reads three bytes and reconstructs the 24-bit constant index.
 *    */
static int constantLongInstruction(const char* name, Chunk* chunk, int offset) {
	  uint32_t idx = ((uint32_t)chunk->code[offset + 1] << 16)
		                 | ((uint32_t)chunk->code[offset + 2] <<  8)
				                |  (uint32_t)chunk->code[offset + 3];
	    printf("%-20s %4u '", name, idx);
	      printValue(chunk->constants.values[idx]);
	        printf("'\n");
		  return offset + 4;   /* opcode + 3-byte operand */
}

//Public API
void disassembleChunk(Chunk* chunk, const char* name) {
	  printf("== %s ==\n", name);
	    for (int offset = 0; offset < chunk->count;) {
		        offset = disassembleInstruction(chunk, offset);
			  }
}

int disassembleInstruction(Chunk* chunk, int offset) {
	  printf("%04d ", offset);

	    /*
	     *    * Challenge 1: use getLine() (RLE-aware) instead of the old
	     *       * parallel int array.
	     *          */
	    int line = getLine(chunk, offset);
	      if (offset > 0 && getLine(chunk, offset - 1) == line) {
		          printf("   | ");
			    } else {
				        printf("%4d ", line);
					  }

	        uint8_t instruction = chunk->code[offset];
		  switch (instruction) {
			      case OP_CONSTANT:
				            return constantInstruction("OP_CONSTANT", chunk, offset);
					        case OP_CONSTANT_LONG:
					          return constantLongInstruction("OP_CONSTANT_LONG", chunk, offset);
						      case OP_RETURN:
						        return simpleInstruction("OP_RETURN", offset);
							    default:
							      printf("Unknown opcode %d\n", instruction);
							            return offset + 1;
								      }
}
