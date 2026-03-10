/*
 *  * memory.c — Challenge 3 (Hardcore)
 *   *
 *    * We call malloc() exactly ONCE at startup to carve out a big pool.
 *     * From then on, reallocate() manages that pool using a classic
 *      * explicit free-list allocator with block headers.
 *       *
 *        * ── Layout of every block ──────────────────────────────────────────
 *         *
 *          *   [ BlockHeader | ... payload bytes ... ]
 *           *
 *            *   BlockHeader fields
 *             *     size    – number of PAYLOAD bytes (does NOT include header size)
 *              *     in_use  – 1 if allocated, 0 if free
 *               *     next    – pointer to the next BlockHeader in the free list
 *                *               (NULL when the block is in-use or is the last free block)
 *                 *
 *                  * ── Allocation strategy ────────────────────────────────────────────
 *                   *   First-fit search through the free list.
 *                    *   When a free block is large enough we split it if the remainder
 *                     *   would hold at least one header + 1 byte of payload.
 *                      *
 *                       * ── Freeing ────────────────────────────────────────────────────────
 *                        *   Mark block free, prepend to free list, then coalesce with any
 *                         *   adjacent free blocks to reduce fragmentation.
 *                          *
 *                           * ── reallocate() contract (matches the rest of clox) ──────────────
 *                            *   oldSize == 0, newSize >  0  → allocate
 *                             *   oldSize >  0, newSize == 0  → free
 *                              *   oldSize >  0, newSize >  0  → resize (alloc new, memcpy, free old)
 *                               */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"

/* ------------------------------------------------------------------ */
/*  Internal types                                                      */
/* ------------------------------------------------------------------ */

typedef struct BlockHeader {
	    size_t             size;   /* payload bytes */
	        int                in_use;
		    struct BlockHeader* next;  /* free-list link (valid only when free) */
} BlockHeader;

#define HEADER_SIZE  (sizeof(BlockHeader))
/* Minimum payload we bother splitting a block for. */
#define MIN_SPLIT    (HEADER_SIZE + 8)

/* ------------------------------------------------------------------ */
/*  Global pool state                                                   */
/* ------------------------------------------------------------------ */

static void*        g_pool      = NULL;   /* raw malloc'd memory       */
static size_t       g_pool_size = 0;
static BlockHeader* g_free_list = NULL;   /* head of the free list      */

/* ------------------------------------------------------------------ */
/*  Helpers                                                             */
/* ------------------------------------------------------------------ */

/* Given a block header, return a pointer to its payload. */
static inline void* payload_of(BlockHeader* h) {
	    return (char*)h + HEADER_SIZE;
}

/* Given a payload pointer, return its block header. */
static inline BlockHeader* header_of(void* p) {
	    return (BlockHeader*)((char*)p - HEADER_SIZE);
}

/* Return the header that physically follows h in the pool. */
static inline BlockHeader* next_physical(BlockHeader* h) {
	    return (BlockHeader*)((char*)h + HEADER_SIZE + h->size);
}

/* Is h still within the pool? */
static inline int in_pool(BlockHeader* h) {
	    return (char*)h >= (char*)g_pool &&
		               (char*)h <  (char*)g_pool + g_pool_size;
}

/* ------------------------------------------------------------------ */
/*  initHeap / freeHeap                                                 */
/* ------------------------------------------------------------------ */

void initHeap(size_t size) {
	    g_pool = malloc(size);            /* the ONE allowed malloc call   */
	        if (g_pool == NULL) {
			        fprintf(stderr, "Could not allocate heap pool.\n");
				        exit(1);
					    }
		    g_pool_size = size;

		        /* Initialise the entire pool as a single free block. */
		        g_free_list         = (BlockHeader*)g_pool;
			    g_free_list->size   = size - HEADER_SIZE;
			        g_free_list->in_use = 0;
				    g_free_list->next   = NULL;
}

void freeHeap(void) {
	    free(g_pool);   /* the ONE allowed free call */
	        g_pool      = NULL;
		    g_pool_size = 0;
		        g_free_list = NULL;
}

/* ------------------------------------------------------------------ */
/*  pool_alloc — first-fit allocation from the pool                    */
/* ------------------------------------------------------------------ */

static void* pool_alloc(size_t size) {
	    /* Align to pointer size so payloads are naturally aligned. */
	    size = (size + sizeof(void*) - 1) & ~(sizeof(void*) - 1);

	        BlockHeader* prev = NULL;
		    BlockHeader* cur  = g_free_list;

		        while (cur != NULL) {
				        if (cur->size >= size) {
						            /* Found a big-enough block. Split if there's room. */
						            size_t remainder = cur->size - size;
							                if (remainder >= MIN_SPLIT) {
										                /* Carve a new free block from the tail. */
										                BlockHeader* split = (BlockHeader*)((char*)cur + HEADER_SIZE + size);
												                split->size   = remainder - HEADER_SIZE;
														                split->in_use = 0;
																                split->next   = cur->next;

																		                cur->size = size;
																				                cur->next = split;   /* temporarily; cleared below */
																						            }
									            /* Remove cur from the free list. */
									            if (prev != NULL) prev->next = cur->next;
										                else              g_free_list = cur->next;

												            cur->in_use = 1;
													                cur->next   = NULL;
															            return payload_of(cur);
																            }
					        prev = cur;
						        cur  = cur->next;
							    }

			    fprintf(stderr, "Out of heap memory (requested %zu bytes).\n", size);
			        exit(1);
}

/* ------------------------------------------------------------------ */
/*  pool_free — return a block to the pool, then coalesce neighbours   */
/* ------------------------------------------------------------------ */

static void pool_free(void* p) {
	    if (p == NULL) return;

	        BlockHeader* h = header_of(p);
		    h->in_use = 0;

		        /* Prepend to free list. */
		        h->next     = g_free_list;
			    g_free_list = h;

			        /*
				 *      * Coalesce: walk the free list and merge any pair of blocks that are
				 *           * physically adjacent.  We repeat until no merge happened (a simple
				 *                * O(n²) approach — good enough for our purposes).
				 *                     */
			        int merged;
				    do {
					            merged = 0;
						            BlockHeader* cur = g_free_list;
							            while (cur != NULL) {
									                BlockHeader* phys_next = next_physical(cur);
											            if (in_pool(phys_next) && !phys_next->in_use) {
													                    /* Remove phys_next from the free list. */
													                    BlockHeader* prev2 = NULL;
															                    BlockHeader* scan  = g_free_list;
																	                    while (scan != NULL && scan != phys_next) {
																				                        prev2 = scan;
																							                    scan  = scan->next;
																									                    }
																			                    if (scan == phys_next) {
																						                        if (prev2 != NULL) prev2->next = phys_next->next;
																									                    else               g_free_list = phys_next->next;
																											                    }
																					                    /* Absorb phys_next into cur. */
																					                    cur->size += HEADER_SIZE + phys_next->size;
																							                    merged = 1;
																									                }
												                cur = cur->next;
														        }
								        } while (merged);
}

/* ------------------------------------------------------------------ */
/*  reallocate — the single memory-management function used by clox    */
/* ------------------------------------------------------------------ */

void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
	    if (newSize == 0) {
		            pool_free(pointer);
			            return NULL;
				        }

	        if (pointer == NULL || oldSize == 0) {
			        return pool_alloc(newSize);
				    }

		    /* Resize: alloc new block, copy, free old. */
		    void* fresh = pool_alloc(newSize);
		        size_t copy_size = oldSize < newSize ? oldSize : newSize;
			    memcpy(fresh, pointer, copy_size);
			        pool_free(pointer);
				    return fresh;
}
