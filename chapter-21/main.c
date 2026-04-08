#include <stdio.h>
#include <string.h>

#include "common.h"
#include "vm.h"
#include "compiler.h"
#include "chunk.h"
#include "debug.h"

/* ------------------------------------------------------------------ */
/*  Run a source string and show the constant table                   */
/* ------------------------------------------------------------------ */
static void runAndShow(const char* label, const char* source) {
	  printf("\n=== %s ===\n", label);
	    printf("Source:\n%s\n", source);

	      /* Compile to a chunk so we can inspect constants before running */
	      Chunk chunk;
	        initChunk(&chunk);
		  if (compile(source, &chunk)) {
			      printf("Constants in chunk (%d total):\n", chunk.constants.count);
			          for (int i = 0; i < chunk.constants.count; i++) {
					        printf("  [%d] ", i);
						      printValue(chunk.constants.values[i]);
						            printf("\n");
							        }
				      printf("Output:\n");
				          vm.chunk = &chunk;
					      vm.ip    = vm.chunk->code;
					          interpret(source);
						    }
		    freeChunk(&chunk);
}

int main(void) {
	  initVM();

	    /* ---- Challenge 1: Deduplication demo ----
	     *    *
	     *       * Without deduplication: each reference to 'x' creates a NEW
	     *          * constant table entry. Three references → three entries for "x".
	     *             *
	     *                * With deduplication: identifierConstant() checks the id cache
	     *                   * and reuses the same index. Three references → ONE entry for "x".
	     *                      */
	    const char* prog1 =
		        "var x = 10;\n"
			    "print x;\n"      /* reference 1 */
			        "print x + 1;\n"  /* reference 2 */
				    "x = x * 2;\n"    /* references 3 and 4 */
				        "print x;\n";     /* reference 5 */

	      printf("\n=== Challenge 1: Identifier Constant Deduplication ===\n");
	        printf("Source:\n%s\n", prog1);
		  Chunk chunk;
		    initChunk(&chunk);
		      if (compile(prog1, &chunk)) {
			          printf("Constants in chunk (%d total):\n", chunk.constants.count);
				      for (int i = 0; i < chunk.constants.count; i++) {
					            printf("  [%d] ", i);
						          printValue(chunk.constants.values[i]);
							        printf("\n");
								    }
				          printf("\nOutput:\n");
					      vm.chunk = &chunk;
					          vm.ip    = vm.chunk->code;
						      /* re-interpret from fresh compile */
						    }
		        freeChunk(&chunk);
			  /* Run it for real output */
			  interpret(prog1);

			    /* ---- General variable tests ---- */
			    interpret("var beverage = \"cafe au lait\";\n"
					                "var breakfast = \"beignets with \" + beverage;\n"
							            "print breakfast;\n");

			      interpret("var a = 1;\n"
					                  "var b = 2;\n"
							              "a = a + b;\n"
								                  "print a;\n");

			        freeVM();
				  return 0;
}
