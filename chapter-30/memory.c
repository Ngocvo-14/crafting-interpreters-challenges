#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "compiler.h"
#include "memory.h"
#include "vm.h"
void* reallocate(void* ptr,size_t oldSize,size_t newSize){
  vm.bytesAllocated+=newSize-oldSize;
  if(newSize>oldSize){
#ifdef DEBUG_STRESS_GC
    collectGarbage();
#endif
    if(vm.bytesAllocated>vm.nextGC)collectGarbage();
  }
  if(newSize==0){free(ptr);return NULL;}
  void* result=realloc(ptr,newSize);
  if(!result){fprintf(stderr,"Out of memory.\n");exit(1);}
  return result;
}
static void freeObject(Obj* obj){
  switch(OBJ_GET_TYPE(obj)){
    case OBJ_BOUND_METHOD:FREE(ObjBoundMethod,obj);break;
    case OBJ_CLASS:{ObjClass* k=(ObjClass*)obj;freeTable(&k->methods);FREE(ObjClass,obj);break;}
    case OBJ_CLOSURE:{ObjClosure* c=(ObjClosure*)obj;FREE_ARRAY(ObjUpvalue*,c->upvalues,c->upvalueCount);FREE(ObjClosure,obj);break;}
    case OBJ_FUNCTION:{ObjFunction* fn=(ObjFunction*)obj;freeChunk(&fn->chunk);FREE(ObjFunction,obj);break;}
    case OBJ_INSTANCE:{ObjInstance* i=(ObjInstance*)obj;freeTable(&i->fields);FREE(ObjInstance,obj);break;}
    case OBJ_NATIVE:FREE(ObjNative,obj);break;
    case OBJ_STRING:{ObjString* s=(ObjString*)obj;FREE_ARRAY(char,s->chars,s->length+1);FREE(ObjString,obj);break;}
    case OBJ_UPVALUE:FREE(ObjUpvalue,obj);break;
  }
}
void freeObjects(void){Obj* obj=vm.objects;while(obj){Obj* next=obj->next;freeObject(obj);obj=next;}free(vm.grayStack);}
void markObject(Obj* object){
  if(object==NULL)return;if(OBJ_IS_MARKED(object)==vm.markBit)return;
  OBJ_SET_MARKED(object,(int)vm.markBit);
  if(vm.grayCapacity<vm.grayCount+1){vm.grayCapacity=GROW_CAPACITY(vm.grayCapacity);vm.grayStack=(Obj**)realloc(vm.grayStack,sizeof(Obj*)*vm.grayCapacity);if(!vm.grayStack){fprintf(stderr,"GC OOM.\n");exit(1);}}
  vm.grayStack[vm.grayCount++]=object;
}
void markValue(Value value){if(IS_OBJ(value))markObject(AS_OBJ(value));}
static void markArray(ValueArray* a){for(int i=0;i<a->count;i++)markValue(a->values[i]);}
static void blackenObject(Obj* object){
  switch(OBJ_GET_TYPE(object)){
    case OBJ_BOUND_METHOD:{ObjBoundMethod* b=(ObjBoundMethod*)object;markValue(b->receiver);markObject((Obj*)b->method);break;}
    case OBJ_CLASS:{ObjClass* k=(ObjClass*)object;markObject((Obj*)k->name);markTable(&k->methods);markObject((Obj*)k->initMethod);markObject((Obj*)k->superclass);break;}
    case OBJ_CLOSURE:{ObjClosure* c=(ObjClosure*)object;markObject((Obj*)c->function);for(int i=0;i<c->upvalueCount;i++)markObject((Obj*)c->upvalues[i]);break;}
    case OBJ_FUNCTION:{ObjFunction* fn=(ObjFunction*)object;markObject((Obj*)fn->name);markArray(&fn->chunk.constants);break;}
    case OBJ_INSTANCE:{ObjInstance* i=(ObjInstance*)object;markObject((Obj*)i->klass);markTable(&i->fields);break;}
    case OBJ_UPVALUE:markValue(((ObjUpvalue*)object)->closed);break;
    case OBJ_NATIVE:case OBJ_STRING:break;
  }
}
static void traceReferences(void){while(vm.grayCount>0){Obj* obj=vm.grayStack[--vm.grayCount];blackenObject(obj);}}
static void markRoots(void){
  for(Value* slot=vm.stack;slot<vm.stackTop;slot++)markValue(*slot);
  for(int i=0;i<vm.frameCount;i++){CallFrame* f=&vm.frames[i];if(f->isClosure)markObject((Obj*)f->closure);else markObject((Obj*)f->fn);}
  for(ObjUpvalue* uv=vm.openUpvalues;uv!=NULL;uv=uv->next)markObject((Obj*)uv);
  markTable(&vm.globals);markCompilerRoots();
}
static void sweep(void){
  Obj* previous=NULL;Obj* object=vm.objects;
  while(object!=NULL){
    if(OBJ_IS_MARKED(object)==vm.markBit){previous=object;object=object->next;}
    else{Obj* unreached=object;object=object->next;if(previous!=NULL)previous->next=object;else vm.objects=object;freeObject(unreached);}
  }
}
void collectGarbage(void){vm.markBit^=1;markRoots();traceReferences();tableRemoveWhite(&vm.strings);sweep();vm.nextGC=vm.bytesAllocated*GC_HEAP_GROW_FACTOR;}
