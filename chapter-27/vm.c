#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "object.h"
#include "memory.h"
#include "table.h"
#include "vm.h"

VM vm;

static void resetStack(){vm.stackTop=vm.stack;vm.frameCount=0;vm.openUpvalues=NULL;}

void initVM(void){
  resetStack();vm.objects=NULL;
  vm.bytesAllocated=0;vm.nextGC=1024*1024;
  vm.markBit=1;vm.grayCount=0;vm.grayCapacity=0;vm.grayStack=NULL;
  initTable(&vm.globals);initTable(&vm.strings);
  extern void registerNatives(void);
  registerNatives();
}
void freeVM(void){freeTable(&vm.globals);freeTable(&vm.strings);freeObjects();}
void push(Value v){*vm.stackTop++=v;}
Value pop(void){return *--vm.stackTop;}
static Value peek(int d){return vm.stackTop[-1-d];}

static void runtimeError(const char* fmt,...){
  va_list args;va_start(args,fmt);vfprintf(stderr,fmt,args);va_end(args);fputs("\n",stderr);
  for(int i=vm.frameCount-1;i>=0;i--){
    CallFrame* f=&vm.frames[i];ObjFunction* fn=frameFunction(f);
    size_t instruction=f->ip-fn->chunk.code-1;
    fprintf(stderr,"[line %d] in ",fn->chunk.lines[instruction]);
    if(!fn->name)fprintf(stderr,"script\n");else fprintf(stderr,"%s()\n",fn->name->chars);
  }
  resetStack();
}

static ObjUpvalue* captureUpvalue(Value* local){
  ObjUpvalue* prev=NULL;ObjUpvalue* uv=vm.openUpvalues;
  while(uv!=NULL&&uv->location>local){prev=uv;uv=uv->next;}
  if(uv!=NULL&&uv->location==local)return uv;
  ObjUpvalue* created=newUpvalue(local);created->next=uv;
  if(prev==NULL)vm.openUpvalues=created;else prev->next=created;
  return created;
}
static void closeUpvalues(Value* last){
  while(vm.openUpvalues!=NULL&&vm.openUpvalues->location>=last){
    ObjUpvalue* uv=vm.openUpvalues;uv->closed=*uv->location;uv->location=&uv->closed;vm.openUpvalues=uv->next;
  }
}

static bool callFunction(ObjFunction* fn,int argc,uint8_t** ip_reg){
  if(argc!=fn->arity){runtimeError("Expected %d arguments but got %d.",fn->arity,argc);return false;}
  if(vm.frameCount==FRAMES_MAX){runtimeError("Stack overflow.");return false;}
  if(vm.frameCount>0)vm.frames[vm.frameCount-1].ip=*ip_reg;
  CallFrame* frame=&vm.frames[vm.frameCount++];
  frame->isClosure=false;frame->fn=fn;frame->ip=fn->chunk.code;frame->slots=vm.stackTop-argc-1;
  *ip_reg=frame->ip;return true;
}
static bool callClosure(ObjClosure* closure,int argc,uint8_t** ip_reg){
  ObjFunction* fn=closure->function;
  if(argc!=fn->arity){runtimeError("Expected %d arguments but got %d.",fn->arity,argc);return false;}
  if(vm.frameCount==FRAMES_MAX){runtimeError("Stack overflow.");return false;}
  if(vm.frameCount>0)vm.frames[vm.frameCount-1].ip=*ip_reg;
  CallFrame* frame=&vm.frames[vm.frameCount++];
  frame->isClosure=true;frame->closure=closure;frame->ip=fn->chunk.code;frame->slots=vm.stackTop-argc-1;
  *ip_reg=frame->ip;return true;
}
static bool callValue(Value callee,int argc,uint8_t** ip_reg){
  if(IS_OBJ(callee)){
    switch(OBJ_TYPE(callee)){
      case OBJ_CLASS: {
        ObjClass* klass=AS_CLASS(callee);
        vm.stackTop[-argc-1]=OBJ_VAL(newInstance(klass));
        return true;
      }
      case OBJ_CLOSURE:  return callClosure(AS_CLOSURE(callee),argc,ip_reg);
      case OBJ_FUNCTION: return callFunction(AS_FUNCTION(callee),argc,ip_reg);
      case OBJ_NATIVE: {
        ObjNative* nat=AS_NATIVE(callee);
        if(nat->arity!=-1&&argc!=nat->arity){runtimeError("Native expected %d args but got %d.",nat->arity,argc);return false;}
        NativeResult result=nat->function(argc,vm.stackTop-argc);
        vm.stackTop-=argc+1;
        if(!result.ok){runtimeError("%s",AS_CSTRING(result.value));return false;}
        push(result.value);return true;
      }
      default:break;
    }
  }
  runtimeError("Can only call functions and classes.");return false;
}

static bool isFalsey(Value v){return IS_NIL(v)||(IS_BOOL(v)&&!AS_BOOL(v));}
static void concatenate(){
  ObjString* b=AS_STRING(peek(0));ObjString* a=AS_STRING(peek(1));
  int length=a->length+b->length;char* chars=ALLOCATE(char,length+1);
  memcpy(chars,a->chars,a->length);memcpy(chars+a->length,b->chars,b->length);chars[length]='\0';
  ObjString* result=takeString(chars,length);pop();pop();push(OBJ_VAL(result));
}

#define SAVE_IP() (vm.frames[vm.frameCount-1].ip=ip)

static InterpretResult run(void){
  CallFrame* frame=&vm.frames[vm.frameCount-1];
  register uint8_t* ip=frame->ip;

#define READ_BYTE()     (*ip++)
#define READ_SHORT()    (ip+=2,(uint16_t)((ip[-2]<<8)|ip[-1]))
#define READ_CONSTANT() (frameFunction(frame)->chunk.constants.values[READ_BYTE()])
#define READ_STRING()   AS_STRING(READ_CONSTANT())
#define BINARY_OP(vt,op) \
  do{if(!IS_NUMBER(peek(0))||!IS_NUMBER(peek(1))){SAVE_IP();runtimeError("Operands must be numbers.");return INTERPRET_RUNTIME_ERROR;} \
    double b=AS_NUMBER(pop()),a=AS_NUMBER(pop());push(vt(a op b));}while(false)

  for(;;){
    switch(READ_BYTE()){
      case OP_CONSTANT: push(READ_CONSTANT()); break;
      case OP_NIL:      push(NIL_VAL); break;
      case OP_TRUE:     push(BOOL_VAL(true)); break;
      case OP_FALSE:    push(BOOL_VAL(false)); break;
      case OP_POP:      pop(); break;
      case OP_GET_LOCAL:{ uint8_t slot=READ_BYTE();push(frame->slots[slot]);break; }
      case OP_SET_LOCAL:{ uint8_t slot=READ_BYTE();frame->slots[slot]=peek(0);break; }
      case OP_GET_GLOBAL:{
        ObjString* name=READ_STRING();Value v;
        if(!tableGet(&vm.globals,name,&v)){SAVE_IP();runtimeError("Undefined variable '%s'.",name->chars);return INTERPRET_RUNTIME_ERROR;}
        push(v);break;
      }
      case OP_DEFINE_GLOBAL:{ ObjString* name=READ_STRING();tableSet(&vm.globals,name,peek(0));pop();break; }
      case OP_SET_GLOBAL:{
        ObjString* name=READ_STRING();
        if(tableSet(&vm.globals,name,peek(0))){tableDelete(&vm.globals,name);SAVE_IP();runtimeError("Undefined variable '%s'.",name->chars);return INTERPRET_RUNTIME_ERROR;}
        break;
      }
      case OP_GET_UPVALUE:{ uint8_t slot=READ_BYTE();push(*frame->closure->upvalues[slot]->location);break; }
      case OP_SET_UPVALUE:{ uint8_t slot=READ_BYTE();*frame->closure->upvalues[slot]->location=peek(0);break; }
      case OP_CLOSE_UPVALUE: closeUpvalues(vm.stackTop-1);pop();break;

      /* ---------------------------------------------------------------
         ch27: OP_GET_PROPERTY
         Challenge 1: instead of aborting on missing field, return nil.
         This allows callers to check: if (obj.field != nil) { ... }
         Python returns AttributeError, Ruby returns nil for missing
         hash keys, JavaScript returns undefined. We chose nil because
         it's the natural "no value" sentinel in Lox, and it allows
         safe checking without try/catch.
      --------------------------------------------------------------- */
      case OP_GET_PROPERTY: {
        if(!IS_INSTANCE(peek(0))){
          SAVE_IP();runtimeError("Only instances have properties.");return INTERPRET_RUNTIME_ERROR;
        }
        ObjInstance* instance=AS_INSTANCE(peek(0));
        ObjString* name=READ_STRING();
        Value value;
        if(tableGet(&instance->fields,name,&value)){
          pop();push(value);
        } else {
          /* Challenge 1: return nil instead of runtime error */
          pop();
          push(NIL_VAL);
        }
        break;
      }

      case OP_SET_PROPERTY: {
        if(!IS_INSTANCE(peek(1))){
          SAVE_IP();runtimeError("Only instances have fields.");return INTERPRET_RUNTIME_ERROR;
        }
        ObjInstance* instance=AS_INSTANCE(peek(1));
        tableSet(&instance->fields,READ_STRING(),peek(0));
        Value value=pop();pop();push(value);
        break;
      }

      /* ---------------------------------------------------------------
         Challenge 2: dynamic field access  obj["fieldname"]
         Stack before: [ ... instance string_key ]
         Pops key and instance, pushes field value (nil if missing).
      --------------------------------------------------------------- */
      case OP_GET_FIELD_DYNAMIC: {
        if(!IS_STRING(peek(0))){
          SAVE_IP();runtimeError("Field name must be a string.");return INTERPRET_RUNTIME_ERROR;
        }
        if(!IS_INSTANCE(peek(1))){
          SAVE_IP();runtimeError("Only instances have fields.");return INTERPRET_RUNTIME_ERROR;
        }
        ObjString* name=AS_STRING(pop());
        ObjInstance* instance=AS_INSTANCE(pop());
        Value value;
        if(tableGet(&instance->fields,name,&value)){push(value);}
        else{push(NIL_VAL);}
        break;
      }

      /* ---------------------------------------------------------------
         Challenge 2: dynamic field set  obj["fieldname"] = value
         Stack before: [ ... instance string_key new_value ]
      --------------------------------------------------------------- */
      case OP_SET_FIELD_DYNAMIC: {
        /* stack: instance, key, value (top) */
        Value new_val=pop();
        if(!IS_STRING(peek(0))){
          SAVE_IP();runtimeError("Field name must be a string.");return INTERPRET_RUNTIME_ERROR;
        }
        if(!IS_INSTANCE(peek(1))){
          SAVE_IP();runtimeError("Only instances have fields.");return INTERPRET_RUNTIME_ERROR;
        }
        ObjString* name=AS_STRING(pop());
        ObjInstance* instance=AS_INSTANCE(pop());
        tableSet(&instance->fields,name,new_val);
        push(new_val);   /* assignment is an expression */
        break;
      }

      /* ---------------------------------------------------------------
         Challenge 3: OP_DELETE_FIELD
         Removes the named field from an instance entirely.
         Stack: [ instance ]  → pops instance, pushes true if deleted.
         Python: del obj.attr
         Ruby:   instance_variable_set(:@name, nil) or remove_instance_variable
         JavaScript: delete obj.prop
         We chose Python/JS style: delete is a statement that removes
         the field completely from the hash table. Returns true if the
         field existed, false if it didn't (like JS delete).
      --------------------------------------------------------------- */
      case OP_DELETE_FIELD: {
        if(!IS_INSTANCE(peek(0))){
          SAVE_IP();runtimeError("Only instances have fields.");return INTERPRET_RUNTIME_ERROR;
        }
        ObjInstance* instance=AS_INSTANCE(pop());
        ObjString* name=READ_STRING();
        bool deleted=tableDelete(&instance->fields,name);
        push(BOOL_VAL(deleted));
        pop(); /* delete is a statement, discard result */
        break;
      }

      case OP_EQUAL:{ Value b=pop(),a=pop();push(BOOL_VAL(valuesEqual(a,b)));break; }
      case OP_GREATER: BINARY_OP(BOOL_VAL,>);break;
      case OP_LESS:    BINARY_OP(BOOL_VAL,<);break;
      case OP_ADD:
        if(IS_STRING(peek(0))&&IS_STRING(peek(1)))concatenate();
        else if(IS_NUMBER(peek(0))&&IS_NUMBER(peek(1))){double b=AS_NUMBER(pop()),a=AS_NUMBER(pop());push(NUMBER_VAL(a+b));}
        else{SAVE_IP();runtimeError("Operands must be two numbers or two strings.");return INTERPRET_RUNTIME_ERROR;}
        break;
      case OP_SUBTRACT: BINARY_OP(NUMBER_VAL,-);break;
      case OP_MULTIPLY: BINARY_OP(NUMBER_VAL,*);break;
      case OP_DIVIDE:   BINARY_OP(NUMBER_VAL,/);break;
      case OP_NOT:      push(BOOL_VAL(isFalsey(pop())));break;
      case OP_NEGATE:
        if(!IS_NUMBER(peek(0))){SAVE_IP();runtimeError("Operand must be a number.");return INTERPRET_RUNTIME_ERROR;}
        *(vm.stackTop-1)=NUMBER_VAL(-AS_NUMBER(*(vm.stackTop-1)));break;
      case OP_PRINT: printValue(pop());printf("\n");break;
      case OP_JUMP:          { uint16_t off=READ_SHORT();ip+=off;break; }
      case OP_JUMP_IF_FALSE: { uint16_t off=READ_SHORT();if(isFalsey(peek(0)))ip+=off;break; }
      case OP_LOOP:          { uint16_t off=READ_SHORT();ip-=off;break; }
      case OP_CALL:{
        int argc=READ_BYTE();SAVE_IP();uint8_t* new_ip=ip;
        if(!callValue(peek(argc),argc,&new_ip))return INTERPRET_RUNTIME_ERROR;
        ip=new_ip;frame=&vm.frames[vm.frameCount-1];break;
      }
      case OP_CLOSURE:{
        ObjFunction* fn=AS_FUNCTION(READ_CONSTANT());
        if(fn->upvalueCount==0){push(OBJ_VAL(fn));}
        else{
          ObjClosure* closure=newClosure(fn);push(OBJ_VAL(closure));
          for(int i=0;i<closure->upvalueCount;i++){
            uint8_t isLocal=READ_BYTE();uint8_t index=READ_BYTE();
            if(isLocal)closure->upvalues[i]=captureUpvalue(frame->slots+index);
            else closure->upvalues[i]=frame->closure->upvalues[index];
          }
        }
        break;
      }
      case OP_CLASS:
        push(OBJ_VAL(newClass(READ_STRING())));
        break;
      case OP_RETURN:{
        Value result=pop();closeUpvalues(frame->slots);vm.frameCount--;
        if(vm.frameCount==0){pop();return INTERPRET_OK;}
        vm.stackTop=frame->slots;push(result);
        frame=&vm.frames[vm.frameCount-1];ip=frame->ip;break;
      }
    }
  }
#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef READ_STRING
#undef BINARY_OP
#undef SAVE_IP
}

InterpretResult interpret(const char* source){
  ObjFunction* fn=compile(source);if(!fn)return INTERPRET_COMPILE_ERROR;
  push(OBJ_VAL(fn));
  CallFrame* frame=&vm.frames[vm.frameCount++];
  frame->isClosure=false;frame->fn=fn;frame->ip=fn->chunk.code;frame->slots=vm.stack;
  return run();
}

/* native library */
static void defineNative(const char* name,NativeFn fn,int arity){push(OBJ_VAL(copyString(name,(int)strlen(name))));push(OBJ_VAL(newNative(fn,arity)));tableSet(&vm.globals,AS_STRING(vm.stack[0]),vm.stack[1]);pop();pop();}
static NativeResult n_clock(int c,Value* a){(void)c;(void)a;return nativeOk(NUMBER_VAL((double)clock()/CLOCKS_PER_SEC));}
static NativeResult n_sqrt(int c,Value* a){(void)c;if(!IS_NUMBER(a[0]))return nativeError("sqrt() requires a number.");double n=AS_NUMBER(a[0]);if(n<0)return nativeError("sqrt() of negative.");return nativeOk(NUMBER_VAL(sqrt(n)));}
static NativeResult n_str(int c,Value* a){(void)c;char buf[64];if(IS_STRING(a[0]))return nativeOk(a[0]);if(IS_NUMBER(a[0])){int len=snprintf(buf,sizeof(buf),"%g",AS_NUMBER(a[0]));return nativeOk(OBJ_VAL(copyString(buf,len)));}if(IS_BOOL(a[0])){const char* b=AS_BOOL(a[0])?"true":"false";return nativeOk(OBJ_VAL(copyString(b,(int)strlen(b))));}return nativeOk(OBJ_VAL(copyString("nil",3)));}
static NativeResult n_type(int c,Value* a){(void)c;const char* t;if(IS_BOOL(a[0]))t="bool";else if(IS_NIL(a[0]))t="nil";else if(IS_NUMBER(a[0]))t="number";else if(IS_STRING(a[0]))t="string";else if(IS_CLASS(a[0]))t="class";else if(IS_INSTANCE(a[0]))t="instance";else if(IS_CLOSURE(a[0])||IS_FUNCTION(a[0]))t="function";else t="native";return nativeOk(OBJ_VAL(copyString(t,(int)strlen(t))));}
void registerNatives(void){
  defineNative("clock", n_clock, 0);
  defineNative("sqrt",  n_sqrt,  1);
  defineNative("str",   n_str,   1);
  defineNative("type",  n_type,  1);
}
