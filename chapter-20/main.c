#include <stdio.h>
#include <string.h>
#include <time.h>

#include "common.h"
#include "table.h"
#include "object.h"
#include "vm.h"

/* ------------------------------------------------------------------ */
/*  Challenge 1: Test hash table with all primitive key types          */
/* ------------------------------------------------------------------ */
static void test_primitive_keys() {
	  printf("=== Challenge 1: Primitive Key Types ===\n");
	    Table t;
	      initTable(&t);

	        /* nil key */
	        tableSet(&t, NIL_VAL, NUMBER_VAL(99));
		  Value v;
		    bool found = tableGet(&t, NIL_VAL, &v);
		      printf("nil key    → %s, value=%g\n", found ? "found" : "miss", AS_NUMBER(v));

		        /* bool keys */
		        tableSet(&t, BOOL_VAL(true),  NUMBER_VAL(1));
			  tableSet(&t, BOOL_VAL(false), NUMBER_VAL(0));
			    tableGet(&t, BOOL_VAL(true),  &v); printf("true key   → %g\n", AS_NUMBER(v));
			      tableGet(&t, BOOL_VAL(false), &v); printf("false key  → %g\n", AS_NUMBER(v));

			        /* number keys */
			        tableSet(&t, NUMBER_VAL(3.14), copyString("pi", 2) ? OBJ_VAL(copyString("pi",2)) : NIL_VAL);
				  tableSet(&t, NUMBER_VAL(42.0), OBJ_VAL(copyString("answer", 6)));
				    tableSet(&t, NUMBER_VAL(0.0),  OBJ_VAL(copyString("zero",   4)));
				      tableGet(&t, NUMBER_VAL(42.0), &v);
				        printf("42.0 key   → %s\n", AS_CSTRING(v));
					  tableGet(&t, NUMBER_VAL(0.0),  &v);
					    printf("0.0 key    → %s\n", AS_CSTRING(v));

					      /* negative zero — must equal 0.0 */
					      tableGet(&t, NUMBER_VAL(-0.0), &v);
					        printf("-0.0 key   → %s (same as 0.0)\n", AS_CSTRING(v));

						  /* string keys */
						  tableSet(&t, OBJ_VAL(copyString("hello", 5)), NUMBER_VAL(123));
						    tableSet(&t, OBJ_VAL(copyString("world", 5)), NUMBER_VAL(456));
						      tableGet(&t, OBJ_VAL(copyString("hello", 5)), &v);
						        printf("\"hello\" key → %g\n", AS_NUMBER(v));

							  /* overwrite */
							  tableSet(&t, NUMBER_VAL(42.0), NUMBER_VAL(999));
							    tableGet(&t, NUMBER_VAL(42.0), &v);
							      printf("overwrite 42.0 → %g\n", AS_NUMBER(v));

							        /* delete */
							        tableDelete(&t, BOOL_VAL(true));
								  found = tableGet(&t, BOOL_VAL(true), &v);
								    printf("after delete true → %s\n", found ? "found" : "gone");

								      freeTable(&t);
								        printf("\n");
}

/* ------------------------------------------------------------------ */
/*  Challenge 3: Benchmark programs                                    */
/* ------------------------------------------------------------------ */

static double now_ms() {
	  return (double)clock() / CLOCKS_PER_SEC * 1000.0;
}

/* Benchmark A: sequential integer keys — tests numeric hashing */
static void bench_sequential_inserts(int n) {
	  Table t; initTable(&t);
	    double start = now_ms();
	      for (int i = 0; i < n; i++) {
		          tableSet(&t, NUMBER_VAL((double)i), NUMBER_VAL((double)i * 2));
			    }
	        double ins = now_ms() - start;

		  start = now_ms();
		    Value v;
		      int hits = 0;
		        for (int i = 0; i < n; i++) {
				    if (tableGet(&t, NUMBER_VAL((double)i), &v)) hits++;
				      }
			  double lkp = now_ms() - start;

			    printf("Sequential int keys (n=%d): insert=%.1fms  lookup=%.1fms  hits=%d\n",
					             n, ins, lkp, hits);
			      freeTable(&t);
}

/* Benchmark B: string keys — tests FNV-1a and interning */
static void bench_string_keys(int n) {
	  Table t; initTable(&t);
	    char buf[32];
	      double start = now_ms();
	        for (int i = 0; i < n; i++) {
			    snprintf(buf, sizeof(buf), "key_%d", i);
			        ObjString* k = copyString(buf, (int)strlen(buf));
				    tableSet(&t, OBJ_VAL(k), NUMBER_VAL((double)i));
				      }
		  double ins = now_ms() - start;

		    start = now_ms();
		      Value v; int hits = 0;
		        for (int i = 0; i < n; i++) {
				    snprintf(buf, sizeof(buf), "key_%d", i);
				        ObjString* k = copyString(buf, (int)strlen(buf));
					    if (tableGet(&t, OBJ_VAL(k), &v)) hits++;
					      }
			  double lkp = now_ms() - start;

			    printf("String keys      (n=%d): insert=%.1fms  lookup=%.1fms  hits=%d\n",
					             n, ins, lkp, hits);
			      freeTable(&t);
}

/* Benchmark C: insert/delete churn — tests tombstone handling */
static void bench_delete_churn(int n) {
	  Table t; initTable(&t);
	    double start = now_ms();
	      for (int i = 0; i < n; i++) {
		          tableSet(&t, NUMBER_VAL((double)i), NIL_VAL);
			      if (i % 2 == 0) tableDelete(&t, NUMBER_VAL((double)(i - 1)));
			        }
	        double elapsed = now_ms() - start;
		  printf("Delete churn     (n=%d): %.1fms  (capacity=%d count=%d)\n",
				           n, elapsed, t.capacity, t.count);
		    freeTable(&t);
}

/* Benchmark D: bool/nil keys — trivially fast, shows lower bound */
static void bench_bool_nil_keys(int n) {
	  Table t; initTable(&t);
	    double start = now_ms();
	      for (int i = 0; i < n; i++) {
		          tableSet(&t, (i % 3 == 0) ? NIL_VAL :
					                   (i % 3 == 1) ? BOOL_VAL(true) : BOOL_VAL(false),
							                NUMBER_VAL((double)i));
			    }
	        double ins = now_ms() - start;
		  printf("Bool/nil keys    (n=%d): insert=%.1fms  (only 3 distinct keys)\n",
				           n, ins);
		    freeTable(&t);
}

int main(void) {
	  initVM();

	    test_primitive_keys();

	      printf("=== Challenge 3: Benchmarks ===\n");
	        bench_sequential_inserts(100000);
		  bench_string_keys(10000);
		    bench_delete_churn(100000);
		      bench_bool_nil_keys(100000);

		        freeVM();
			  return 0;
}
