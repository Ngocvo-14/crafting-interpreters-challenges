#ifndef clox_memory_h
#define clox_memory_h
#include "common.h"
#include "object.h"

#define ALLOCATE(type, count)        (type*)reallocate(NULL, 0, sizeof(type)*(count))
#define FREE(type, ptr)              reallocate(ptr, sizeof(type), 0)
#define GROW_CAPACITY(cap)           ((cap) < 8 ? 8 : (cap)*2)
#define GROW_ARRAY(type,ptr,old,new) (type*)reallocate(ptr,sizeof(type)*(old),sizeof(type)*(new))
#define FREE_ARRAY(type,ptr,old)     reallocate(ptr,sizeof(type)*(old),0)

#define GC_HEAP_GROW_FACTOR 2

void* reallocate(void* ptr, size_t oldSize, size_t newSize);
void  markObject(Obj* object);
void  markValue (Value value);
void  collectGarbage(void);
void  freeObjects(void);
#endif
