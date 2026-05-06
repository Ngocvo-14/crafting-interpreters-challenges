#include <stdio.h>
#include <string.h>
#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"
#define ALLOCATE_OBJ(type,objectType) (type*)allocateObject(sizeof(type),objectType)
static Obj* allocateObject(size_t size,ObjType type){
  Obj* obj=(Obj*)reallocate(NULL,0,size);OBJ_INIT(obj,type);obj->next=vm.objects;vm.objects=obj;return obj;
}
ObjBoundMethod* newBoundMethod(Value receiver,ObjClosure* method){ObjBoundMethod* bound=ALLOCATE_OBJ(ObjBoundMethod,OBJ_BOUND_METHOD);bound->receiver=receiver;bound->method=method;return bound;}
ObjClass* newClass(ObjString* name){ObjClass* klass=ALLOCATE_OBJ(ObjClass,OBJ_CLASS);klass->name=name;klass->initMethod=NULL;klass->superclass=NULL;initTable(&klass->methods);return klass;}
ObjClosure* newClosure(ObjFunction* function){ObjUpvalue** upvalues=ALLOCATE(ObjUpvalue*,function->upvalueCount);for(int i=0;i<function->upvalueCount;i++)upvalues[i]=NULL;ObjClosure* closure=ALLOCATE_OBJ(ObjClosure,OBJ_CLOSURE);closure->function=function;closure->upvalues=upvalues;closure->upvalueCount=function->upvalueCount;return closure;}
ObjFunction* newFunction(void){ObjFunction* fn=ALLOCATE_OBJ(ObjFunction,OBJ_FUNCTION);fn->arity=0;fn->upvalueCount=0;fn->name=NULL;initChunk(&fn->chunk);return fn;}
ObjInstance* newInstance(ObjClass* klass){ObjInstance* instance=ALLOCATE_OBJ(ObjInstance,OBJ_INSTANCE);instance->klass=klass;initTable(&instance->fields);return instance;}
ObjNative* newNative(NativeFn fn,int arity){ObjNative* native=ALLOCATE_OBJ(ObjNative,OBJ_NATIVE);native->function=fn;native->arity=arity;return native;}
ObjUpvalue* newUpvalue(Value* slot){ObjUpvalue* uv=ALLOCATE_OBJ(ObjUpvalue,OBJ_UPVALUE);uv->closed=NIL_VAL;uv->location=slot;uv->next=NULL;return uv;}
NativeResult nativeError(const char* msg){ObjString* s=copyString(msg,(int)strlen(msg));return (NativeResult){false,OBJ_VAL(s)};}
static uint32_t hashString(const char* key,int length){uint32_t hash=2166136261u;for(int i=0;i<length;i++){hash^=(uint8_t)key[i];hash*=16777619;}return hash;}
static ObjString* allocateString(char* chars,int length,uint32_t hash){
  ObjString* s=ALLOCATE_OBJ(ObjString,OBJ_STRING);s->length=length;s->chars=chars;s->hash=hash;
  push(OBJ_VAL(s));tableSet(&vm.strings,s,NIL_VAL);pop();return s;
}

/* ================================================================
   SSO: takeString / copyString return a VAL_SHORT_STRING Value
   for strings <= SSO_MAX chars. The string is still interned in
   vm.strings (using a heap ObjString as the key) so we get
   de-duplication, but the Value handed back to user code is
   stack-allocated — no GC overhead for the common case.

   For strings > SSO_MAX chars, we fall through to the original
   heap-allocated ObjString path.
================================================================ */

ObjString* takeString(char* chars,int length){
  uint32_t hash=hashString(chars,length);
  ObjString* interned=tableFindString(&vm.strings,chars,length,hash);
  if(interned){FREE_ARRAY(char,chars,length+1);return interned;}
  return allocateString(chars,length,hash);
}

/* copyString returns a Value (not ObjString*) so callers get SSO benefit */
Value copyStringValue(const char* chars, int length) {
  if (length <= SSO_MAX) {
    /* Short string: return inline Value, no heap alloc */
    return makeShortString(chars, length);
  }
  /* Long string: heap allocate as before */
  uint32_t hash=hashString(chars,length);
  ObjString* interned=tableFindString(&vm.strings,chars,length,hash);
  if(interned) return OBJ_VAL(interned);
  char* heapChars=ALLOCATE(char,length+1);
  memcpy(heapChars,chars,length);heapChars[length]='\0';
  return OBJ_VAL(allocateString(heapChars,length,hash));
}

ObjString* copyString(const char* chars,int length){
  uint32_t hash=hashString(chars,length);
  ObjString* interned=tableFindString(&vm.strings,chars,length,hash);
  if(interned)return interned;
  char* heapChars=ALLOCATE(char,length+1);
  memcpy(heapChars,chars,length);heapChars[length]='\0';
  return allocateString(heapChars,length,hash);
}

static void printFunction(ObjFunction* fn){if(!fn->name){printf("<script>");return;}printf("<fn %s>",fn->name->chars);}
void printObject(Value v){
  switch(OBJ_TYPE(v)){
    case OBJ_BOUND_METHOD:printFunction(AS_BOUND_METHOD(v)->method->function);break;
    case OBJ_CLASS:printf("%s",AS_CLASS(v)->name->chars);break;
    case OBJ_CLOSURE:printFunction(AS_CLOSURE(v)->function);break;
    case OBJ_FUNCTION:printFunction(AS_FUNCTION(v));break;
    case OBJ_INSTANCE:printf("%s instance",AS_INSTANCE(v)->klass->name->chars);break;
    case OBJ_NATIVE:printf("<native fn>");break;
    case OBJ_STRING:printf("%s",AS_CSTRING(v));break;
    case OBJ_UPVALUE:printf("upvalue");break;
  }
}
