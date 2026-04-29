#include <stdlib.h>
#include "chunk.h"
#include "memory.h"
#include "vm.h"

void initChunk(Chunk* c) {
  c->count=0; c->capacity=0; c->code=NULL; c->lines=NULL;
  initValueArray(&c->constants);
}
void freeChunk(Chunk* c) {
  FREE_ARRAY(uint8_t,c->code,c->capacity);
  FREE_ARRAY(int,c->lines,c->capacity);
  freeValueArray(&c->constants);
  initChunk(c);
}
void writeChunk(Chunk* c, uint8_t byte, int line) {
  if(c->capacity<c->count+1){
    int old=c->capacity; c->capacity=GROW_CAPACITY(old);
    c->code =GROW_ARRAY(uint8_t,c->code, old,c->capacity);
    c->lines=GROW_ARRAY(int,    c->lines,old,c->capacity);
  }
  c->code[c->count]=byte; c->lines[c->count]=line; c->count++;
}

/* ch26: push constant onto VM stack before adding to table so GC can
   find it if a collection is triggered during the table resize */
int addConstant(Chunk* c, Value value) {
  push(value);
  writeValueArray(&c->constants, value);
  pop();
  return c->constants.count - 1;
}

int getLine(Chunk* c, int offset) { return c->lines[offset]; }
