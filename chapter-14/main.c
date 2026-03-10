#include <stdio.h>

#include "common.h"
#include "chunk.h"
#include "debug.h"
#include "memory.h"

int main(int argc, const char* argv[]) {
	  /* ----------------------------------------------------------------
	   *    * Challenge 3: initialise our custom heap BEFORE anything else.
	   *       * ---------------------------------------------------------------- */
	  initHeap(HEAP_SIZE);

	    /* ================================================================
	     *    * Test 1: basic RLE line encoding (Challenge 1)
	     *       * ================================================================
	     *          * We'll write several instructions spanning only two source lines
	     *             * and confirm that getLine() decodes them correctly.
	     *                */
	    printf("=== Test 1: RLE line encoding ===\n");
	      {
		          Chunk chunk;
			      initChunk(&chunk);

			          /* Three instructions on line 10 */
			          int c1 = addConstant(&chunk, 1.0);
				      writeChunk(&chunk, OP_CONSTANT, 10);
				          writeChunk(&chunk, (uint8_t)c1, 10);

					      int c2 = addConstant(&chunk, 2.0);
					          writeChunk(&chunk, OP_CONSTANT, 10);
						      writeChunk(&chunk, (uint8_t)c2, 10);

						          /* Two instructions on line 11 */
						          writeChunk(&chunk, OP_RETURN, 11);

							      /* Verify RLE storage: should be 2 runs instead of 5 entries */
							      printf("  Bytecode bytes written : %d\n", chunk.count);
							          printf("  RLE runs stored        : %d  (vs %d without RLE)\n",
										             chunk.lines.count, chunk.count);

								      /* Spot-check getLine */
								      for (int i = 0; i < chunk.count; i++) {
									            printf("  offset %d -> line %d\n", i, getLine(&chunk, i));
										        }

								          printf("\nDisassembly:\n");
									      disassembleChunk(&chunk, "test1-rle");
									          freeChunk(&chunk);
										    }

	        /* ================================================================
		 *    * Test 2: writeConstant auto-selects short vs long (Challenge 2)
		 *       * ================================================================
		 *          * We add 257 constants.  The first 256 should use OP_CONSTANT,
		 *             * the 257th should automatically use OP_CONSTANT_LONG.
		 *                */
	        printf("\n=== Test 2: OP_CONSTANT vs OP_CONSTANT_LONG ===\n");
		  {
			      Chunk chunk;
			          initChunk(&chunk);

				      /* Add 256 constants (indices 0–255) – each uses OP_CONSTANT. */
				      for (int i = 0; i < 256; i++) {
					            writeConstant(&chunk, (double)i, 1);
						        }

				          /* The 257th constant (index 256) – must use OP_CONSTANT_LONG. */
				          writeConstant(&chunk, 999.0, 2);
					      writeChunk(&chunk, OP_RETURN, 2);

					          printf("  Total constants stored : %d\n", chunk.constants.count);

						      /* Disassemble just the last few instructions to keep output short. */
						      printf("\nDisassembly of last 3 instructions "
								                 "(255th constant, 256th constant, RETURN):\n");
						          /*
							   *      * Each OP_CONSTANT occupies 2 bytes, so the 255th starts at offset
							   *           * (255-1)*2 = 508.  The 256th (LONG) starts at 510, takes 4 bytes.
							   *                * RETURN is at 514.
							   *                     */
						          int start = (255 - 1) * 2;
							      for (int off = start; off < chunk.count; ) {
								            off = disassembleInstruction(&chunk, off);
									        }

							          freeChunk(&chunk);
								    }

		    /* ================================================================
		     *    * Test 3: custom allocator stress test (Challenge 3)
		     *       * ================================================================
		     *          * Repeatedly allocate and free chunks to exercise pool_alloc,
		     *             * pool_free, splitting, and coalescing.
		     *                */
		    printf("\n=== Test 3: custom heap allocator ===\n");
		      {
			          /* Allocate a bunch of small chunks, free some, allocate again. */
			          Chunk chunks[8];
				      for (int i = 0; i < 8; i++) {
					            initChunk(&chunks[i]);
						          for (int j = 0; j < 10; j++) {
								          writeChunk(&chunks[i], OP_RETURN, j + 1);
									        }
							      }

				          /* Free every other chunk. */
				          for (int i = 0; i < 8; i += 2) {
						        freeChunk(&chunks[i]);
							    }

					      /* Allocate again into the freed slots. */
					      for (int i = 0; i < 8; i += 2) {
						            initChunk(&chunks[i]);
							          writeConstant(&chunks[i], (double)(i * 100), 1);
								        writeChunk(&chunks[i], OP_RETURN, 1);
									    }

					          /* Verify they still disassemble correctly. */
					          for (int i = 0; i < 8; i++) {
							        char name[32];
								      snprintf(name, sizeof(name), "stress-chunk-%d", i);
								            disassembleChunk(&chunks[i], name);
									        }

						      /* Clean up. */
						      for (int i = 0; i < 8; i++) {
							            freeChunk(&chunks[i]);
								        }
						        }

		        printf("\nAll tests passed.\n");

			  /* Release the pool at exit. */
			  freeHeap();
			    return 0;
}
