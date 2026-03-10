#include <stdio.h>
#include <string.h>
#include <time.h>

#include "common.h"
#include "chunk.h"
#include "debug.h"
#include "memory.h"
#include "vm.h"

/* ------------------------------------------------------------------ */
/*  Helper: emit a constant load                                        */
/* ------------------------------------------------------------------ */
static void emitConstant(Chunk* c, double v, int line) {
	  writeConstant(c, v, line);
}

/* ================================================================== */
/*  Challenge 1 – Bytecode sequences for the four expressions          */
/* ================================================================== */

/*
 *  * 1a)  1 * 2 + 3
 *   *
 *    *  Precedence: (1 * 2) + 3
 *     *
 *      *  CONSTANT 1
 *       *  CONSTANT 2
 *        *  MULTIPLY          → stack: [2]
 *         *  CONSTANT 3
 *          *  ADD               → stack: [5]
 *           *  RETURN            prints 5
 *            */
static void test_expr_1a() {
	  printf("\n--- 1a: 1 * 2 + 3  (expect 5) ---\n");
	    Chunk c; initChunk(&c);
	      emitConstant(&c, 1, 1); emitConstant(&c, 2, 1);
	        writeChunk(&c, OP_MULTIPLY, 1);
		  emitConstant(&c, 3, 1);
		    writeChunk(&c, OP_ADD, 1);
		      writeChunk(&c, OP_RETURN, 1);
		        disassembleChunk(&c, "1*2+3");
			  interpret(&c);
			    freeChunk(&c);
}

/*
 *  * 1b)  1 + 2 * 3
 *   *
 *    *  Precedence: 1 + (2 * 3)
 *     *
 *      *  CONSTANT 1
 *       *  CONSTANT 2
 *        *  CONSTANT 3
 *         *  MULTIPLY          → stack: [1, 6]
 *          *  ADD               → stack: [7]
 *           *  RETURN            prints 7
 *            */
static void test_expr_1b() {
	  printf("\n--- 1b: 1 + 2 * 3  (expect 7) ---\n");
	    Chunk c; initChunk(&c);
	      emitConstant(&c, 1, 1);
	        emitConstant(&c, 2, 1); emitConstant(&c, 3, 1);
		  writeChunk(&c, OP_MULTIPLY, 1);
		    writeChunk(&c, OP_ADD, 1);
		      writeChunk(&c, OP_RETURN, 1);
		        disassembleChunk(&c, "1+2*3");
			  interpret(&c);
			    freeChunk(&c);
}

/*
 *  * 1c)  3 - 2 - 1
 *   *
 *    *  Subtraction is left-associative: (3 - 2) - 1
 *     *
 *      *  CONSTANT 3
 *       *  CONSTANT 2
 *        *  SUBTRACT          → stack: [1]
 *         *  CONSTANT 1
 *          *  SUBTRACT          → stack: [0]
 *           *  RETURN            prints 0
 *            */
static void test_expr_1c() {
	  printf("\n--- 1c: 3 - 2 - 1  (expect 0) ---\n");
	    Chunk c; initChunk(&c);
	      emitConstant(&c, 3, 1); emitConstant(&c, 2, 1);
	        writeChunk(&c, OP_SUBTRACT, 1);
		  emitConstant(&c, 1, 1);
		    writeChunk(&c, OP_SUBTRACT, 1);
		      writeChunk(&c, OP_RETURN, 1);
		        disassembleChunk(&c, "3-2-1");
			  interpret(&c);
			    freeChunk(&c);
}

/*
 *  * 1d)  1 + 2 * 3 - 4 / -5
 *   *
 *    *  Precedence: 1 + (2 * 3) - (4 / (-5))
 *     *              = 1 + 6 - (-0.8)
 *      *              = 7.8
 *       *
 *        *  Note: Lox has no negative literals, so -5 = CONSTANT 5 + NEGATE.
 *         *
 *          *  CONSTANT 1
 *           *  CONSTANT 2
 *            *  CONSTANT 3
 *             *  MULTIPLY          → stack: [1, 6]
 *              *  ADD               → stack: [7]
 *               *  CONSTANT 4
 *                *  CONSTANT 5
 *                 *  NEGATE            → stack: [7, 4, -5]
 *                  *  DIVIDE            → stack: [7, -0.8]
 *                   *  SUBTRACT          → stack: [7.8]
 *                    *  RETURN            prints 7.8
 *                     */
static void test_expr_1d() {
	  printf("\n--- 1d: 1 + 2*3 - 4/-5  (expect 7.8) ---\n");
	    Chunk c; initChunk(&c);
	      emitConstant(&c, 1, 1);
	        emitConstant(&c, 2, 1); emitConstant(&c, 3, 1);
		  writeChunk(&c, OP_MULTIPLY, 1);
		    writeChunk(&c, OP_ADD, 1);
		      emitConstant(&c, 4, 1);
		        emitConstant(&c, 5, 1);
			  writeChunk(&c, OP_NEGATE, 1);
			    writeChunk(&c, OP_DIVIDE, 1);
			      writeChunk(&c, OP_SUBTRACT, 1);
			        writeChunk(&c, OP_RETURN, 1);
				  disassembleChunk(&c, "1+2*3-4/-5");
				    interpret(&c);
				      freeChunk(&c);
}

/* ================================================================== */
/*  Challenge 1 (part 2) – Minimal instruction set: 4 - 3 * -2        */
/*                                                                      */
/*  Normal: 4 - (3 * (-2)) = 4 - (-6) = 10                           */
/* ================================================================== */

/*
 *  * WITHOUT OP_NEGATE
 *   *
 *    *  -x  ≡  0 - x    (subtract from zero)
 *     *
 *      *  So  -2  becomes:  CONSTANT 0 / CONSTANT 2 / SUBTRACT
 *       *
 *        *  CONSTANT 4
 *         *  CONSTANT 3
 *          *  CONSTANT 0        ← the zero used to negate
 *           *  CONSTANT 2
 *            *  SUBTRACT          → stack: [4, 3, -2]
 *             *  MULTIPLY          → stack: [4, -6]
 *              *  SUBTRACT          → stack: [10]
 *               *  RETURN
 *                */
static void test_no_negate() {
	  printf("\n--- No OP_NEGATE: 4 - 3 * -2  (expect 10) ---\n");
	    Chunk c; initChunk(&c);
	      emitConstant(&c, 4, 1);
	        emitConstant(&c, 3, 1);
		  emitConstant(&c, 0, 1); emitConstant(&c, 2, 1);
		    writeChunk(&c, OP_SUBTRACT, 1);   /* 0 - 2 = -2 */
		      writeChunk(&c, OP_MULTIPLY, 1);   /* 3 * -2 = -6 */
		        writeChunk(&c, OP_SUBTRACT, 1);   /* 4 - -6 = 10 */
			  writeChunk(&c, OP_RETURN, 1);
			    disassembleChunk(&c, "no-negate");
			      interpret(&c);
			        freeChunk(&c);
}

/*
 *  * WITHOUT OP_SUBTRACT
 *   *
 *    *  a - b  ≡  a + (-b)   (add the negation)
 *     *
 *      *  CONSTANT 4
 *       *  CONSTANT 3
 *        *  CONSTANT 2
 *         *  NEGATE            → stack: [4, 3, -2]
 *          *  MULTIPLY          → stack: [4, -6]
 *           *  NEGATE            → stack: [4, 6]   (negate -6 to get +6)
 *            *  ADD               → stack: [10]     (4 + 6)
 *             *  RETURN
 *              */
static void test_no_subtract() {
	  printf("\n--- No OP_SUBTRACT: 4 - 3 * -2  (expect 10) ---\n");
	    Chunk c; initChunk(&c);
	      emitConstant(&c, 4, 1);
	        emitConstant(&c, 3, 1);
		  emitConstant(&c, 2, 1);
		    writeChunk(&c, OP_NEGATE, 1);     /* -2 */
		      writeChunk(&c, OP_MULTIPLY, 1);   /* 3 * -2 = -6 */
		        writeChunk(&c, OP_NEGATE, 1);     /* -(-6) = 6 */
			  writeChunk(&c, OP_ADD, 1);        /* 4 + 6 = 10 */
			    writeChunk(&c, OP_RETURN, 1);
			      disassembleChunk(&c, "no-subtract");
			        interpret(&c);
				  freeChunk(&c);
}

/* ================================================================== */
/*  Challenge 2 – Dynamic stack overflow test                          */
/* ================================================================== */

/*
 *  * Push far more values than STACK_INITIAL (256) to force the stack
 *   * to grow dynamically.  We push 600 constants then pair-wise add
 *    * them back down to a single result.
 *     */
static void test_stack_overflow() {
	  printf("\n--- Challenge 2: dynamic stack growth (600 pushes) ---\n");
	    Chunk c; initChunk(&c);

	      int N = 600;
	        for (int i = 0; i < N; i++) {
			    emitConstant(&c, 1.0, 1);   /* push 1.0, N times */
			      }
		  /* Add pairs until one value remains */
		  for (int i = 0; i < N - 1; i++) {
			      writeChunk(&c, OP_ADD, 1);
			        }
		    writeChunk(&c, OP_RETURN, 1);  /* should print 600 */

		      interpret(&c);
		        freeChunk(&c);
			  printf("Stack grew past initial %d slots without crashing.\n", STACK_INITIAL);
}

/* ================================================================== */
/*  Challenge 3 – In-place negate benchmark                            */
/* ================================================================== */

/*
 *  * Run a tight loop of 10 million negations and time it.
 *   * We run two separate chunks:
 *    *   A) uses the normal push(-pop()) path  → comment out NEGATE_IN_PLACE
 *     *      in vm.c to compare; here we just time the optimised version.
 *      *   B) same sequence but with the in-place macro active (default).
 *       *
 *        * Since we compiled vm.c with the in-place macro, this just shows
 *         * the timing for the optimised version.  The comment in vm.c
 *          * explains the approach for both.
 *           */
static void test_negate_benchmark() {
	  printf("\n--- Challenge 3: negate-in-place timing ---\n");
	    Chunk c; initChunk(&c);

	      /* seed value */
	      emitConstant(&c, 1.0, 1);

	        int ITERS = 10000000;
		  for (int i = 0; i < ITERS; i++) {
			      writeChunk(&c, OP_NEGATE, 1);
			        }
		    writeChunk(&c, OP_RETURN, 1);

		      clock_t start = clock();
		        interpret(&c);
			  clock_t end = clock();

			    printf("10M negations in %.3f seconds (in-place optimisation active).\n",
					             (double)(end - start) / CLOCKS_PER_SEC);
			      freeChunk(&c);
}

/* ================================================================== */
/*  main                                                                */
/* ================================================================== */

int main(int argc, const char* argv[]) {
	  (void)argc; (void)argv;

	    initHeap(HEAP_SIZE);
	      initVM();

	        /* Challenge 1 – four expressions */
	        test_expr_1a();
		  test_expr_1b();
		    test_expr_1c();
		      test_expr_1d();

		        /* Challenge 1 part 2 – minimal instruction set */
		        test_no_negate();
			  test_no_subtract();

			    /* Challenge 2 – dynamic stack */
			    test_stack_overflow();

			      /* Challenge 3 – in-place negate */
			      test_negate_benchmark();

			        freeVM();
				  freeHeap();
				    return 0;
}
