#include <stdio.h>
#include <string.h>

#include "common.h"
#include "vm.h"

static void runTest(const char* label, const char* source) {
	  printf("--- %s ---\n", label);
	    interpret(source);
}

int main(void) {
	  initVM();

	    /* Basic string operations */
	    runTest("string literal",           "\"hello, world\"");
	      runTest("string equality true",     "\"abc\" == \"abc\"");
	        runTest("string equality false",    "\"abc\" == \"def\"");
		  runTest("concatenation",            "\"foo\" + \"bar\"");
		    runTest("concat chain",             "\"st\" + \"ri\" + \"ng\"");

		      /* Challenge 1+2: FAM + ownership demo */
		      printf("\n--- Challenge 1+2: FAM single-alloc + const strings ---\n");
		        /*
			 *    * "hello" → constString (no copy, points into source buffer, owned=false)
			 *       * "hello" + "!" → concatString → copyString (FAM, owned=true)
			 *          */
		        runTest("const + concat",           "\"hello\" + \"!\"");

			  /* Challenge 3: mixed-type + */
			  printf("\n--- Challenge 3: mixed-type + (string coercion) ---\n");
			    runTest("string + number",          "\"Value: \" + 42");
			      runTest("number + string",          "100 + \" bottles\"");
			        runTest("string + bool",            "\"Result: \" + true");
				  runTest("string + nil",             "\"Got: \" + nil");
				    runTest("number + number (unchanged)", "1 + 2");

				      freeVM();
				        return 0;
}
