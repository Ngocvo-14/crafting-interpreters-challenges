#ifndef clox_memory_h
#define clox_memory_h

#include "common.h"

#define GROW_CAPACITY(capacity) \
	    ((capacity) < 8 ? 8 : (capacity) * 2)

#define GROW_ARRAY(type, pointer, oldCount, newCount) \
	    (type*)reallocate(pointer, sizeof(type) * (oldCount), \
			            sizeof(type) * (newCount))

#define FREE_ARRAY(type, pointer, oldCount) \
	    reallocate(pointer, sizeof(type) * (oldCount), 0)

/* ---------------------------------------------------------------
 *  * Challenge 3 (Hardcore): Custom heap allocator.
 *   *
 *    * Call initHeap() ONCE at program startup with a desired heap
 *     * size. All subsequent reallocate() calls draw from that pool.
 *      * Call freeHeap() at exit to release the single malloc'd block.
 *       * --------------------------------------------------------------- */
#define HEAP_SIZE (1024 * 1024 * 64)   /* 4 MB pool – tune as needed */

void  initHeap(size_t size);
void  freeHeap(void);
void* reallocate(void* pointer, size_t oldSize, size_t newSize);

#endif
