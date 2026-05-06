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
  resetStack();vm.objects=NULL;vm.bytesAllocated=0;vm.nextGC=1024*1024;
  vm.markBit=1;vm.grayCount=0;vm.grayCapacity=0;vm.grayStack=NULL;
  initTable(&vm.globals);initTable(&vm.strings);
  extern void registerNatives(void);registerNatives();
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
  if(prev==NULL)vm.openUpvalues=created;else prev->next=created;return created;
}
static void closeUpvalues(Value* last){
  while(vm.openUpvalues!=NULL&&vm.openUpvalues->location>=last){
    ObjUpvalue* uv=vm.openUpvalues;uv->closed=*uv->location;uv->location=&uv->closed;vm.openUpvalues=uv->next;
  }
}
static bool call(ObjClosure* closure,int argc,uint8_t** ip_reg){
  ObjFunction* fn=closure->function;
  if(argc!=fn->arity){runtimeError("Expected %d arguments but got %d.",fn->arity,argc);return false;}
  if(vm.frameCount==FRAMES_MAX){runtimeError("Stack overflow.");return false;}
  if(vm.frameCount>0)vm.frames[vm.frameCount-1].ip=*ip_reg;
  CallFrame* frame=&vm.frames[vm.frameCount++];
  frame->isClosure=true;frame->closure=closure;frame->ip=fn->chunk.code;frame->slots=vm.stackTop-argc-1;
  *ip_reg=frame->ip;return true;
}
static bool bindMethod(ObjClass* klass,ObjString* name){
  Value method;
  if(!tableGet(&klass->methods,name,&method)){runtimeError("Undefined property '%s'.",name->chars);return false;}
  ObjBoundMethod* bound=newBoundMethod(peek(0),AS_CLOSURE(method));pop();push(OBJ_VAL(bound));return true;
}
static bool invokeFromClass(ObjClass* klass,ObjString* name,int argCount,uint8_t** ip_reg){
  Value method;
  if(!tableGet(&klass->methods,name,&method)){runtimeError("Undefined property '%s'.",name->chars);return false;}
  return call(AS_CLOSURE(method),argCount,ip_reg);
}

/* ----------------------------------------------------------------
   BETA helper: find the root class that defines a given method
   by walking the superclass chain from concrete class upward.
   Returns the highest ancestor whose method table contains `name`
   with a *different* closure than the current frame's closure
   (so we don't return the method itself).

   For BETA dispatch: when BostonCream().cook() is called,
   we want to run Doughnut.cook first (highest in chain).
   invokeTopOfChain walks up to the root, finds the highest
   class that has 'name', and calls that.
---------------------------------------------------------------- */
static bool invokeTopOfChain(ObjInstance* instance, ObjString* name,
                              int argCount, uint8_t** ip_reg) {
  /* Walk to the top of the chain, collecting classes */
  ObjClass* chain[64];
  int chainLen = 0;
  ObjClass* k = instance->klass;
  while (k != NULL && chainLen < 64) { chain[chainLen++] = k; k = k->superclass; }

  /* Find the highest (last in chain array) class that defines name */
  ObjClosure* topClosure = NULL;
  for (int i = chainLen - 1; i >= 0; i--) {
    Value m;
    if (tableGet(&chain[i]->methods, name, &m) && IS_CLOSURE(m)) {
      topClosure = AS_CLOSURE(m);
      break;
    }
  }
  if (topClosure == NULL) {
    runtimeError("Undefined method '%s'.", name->chars);
    return false;
  }
  /* Wire receiver into slot 0 */
  vm.stackTop[-argCount - 1] = OBJ_VAL(instance);
  return call(topClosure, argCount, ip_reg);
}

static bool callValue(Value callee,int argc,uint8_t** ip_reg){
  if(IS_OBJ(callee)){
    switch(OBJ_TYPE(callee)){
      case OBJ_BOUND_METHOD:{
        ObjBoundMethod* bound=AS_BOUND_METHOD(callee);
        vm.stackTop[-argc-1]=bound->receiver;
        return call(bound->method,argc,ip_reg);
      }
      case OBJ_CLASS:{
        ObjClass* klass=AS_CLASS(callee);
        vm.stackTop[-argc-1]=OBJ_VAL(newInstance(klass));
        if(klass->initMethod!=NULL){return call(klass->initMethod,argc,ip_reg);}
        else if(argc!=0){runtimeError("Expected 0 arguments but got %d.",argc);return false;}
        return true;
      }
      case OBJ_CLOSURE: return call(AS_CLOSURE(callee),argc,ip_reg);
      case OBJ_NATIVE:{
        ObjNative* nat=AS_NATIVE(callee);
        if(nat->arity!=-1&&argc!=nat->arity){runtimeError("Native expected %d args, got %d.",nat->arity,argc);return false;}
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

/* ----------------------------------------------------------------
   BETA invoke: instead of the subclass method winning, the ROOT
   class method wins. We walk up the superclass chain to the top.
---------------------------------------------------------------- */
static bool invoke(ObjString* name,int argCount,uint8_t** ip_reg){
  Value receiver=peek(argCount);
  if(!IS_INSTANCE(receiver)){runtimeError("Only instances have methods.");return false;}
  ObjInstance* instance=AS_INSTANCE(receiver);
  /* Fields shadow methods (kept from ch27) */
  Value value;
  if(tableGet(&instance->fields,name,&value)){
    vm.stackTop[-argCount-1]=value;return callValue(value,argCount,ip_reg);
  }
  /* BETA: call top-of-chain method */
  return invokeTopOfChain(instance, name, argCount, ip_reg);
}

static bool isFalsey(Value v){return IS_NIL(v)||(IS_BOOL(v)&&!AS_BOOL(v));}
static void concatenate(){
  /* Use local copies of the Values so pointers are stable.
     For SSO: copy chars immediately into fixed buffers to avoid
     any pointer aliasing issues with C stack optimization. */
  Value bVal = peek(0);
  Value aVal = peek(1);
  
  char aBuf[SSO_MAX+1];
  char bBuf[SSO_MAX+1];
  const char* aChars;
  const char* bChars;
  int aLen, bLen;
  
  if (aVal.type == VAL_SHORT_STRING) {
    aLen = SSO_LENGTH(aVal);
    memcpy(aBuf, aVal.as.shortStr, aLen);
    aBuf[aLen] = '\0';
    aChars = aBuf;
  } else {
    ObjString* s = (ObjString*)AS_OBJ(aVal);
    aChars = s->chars;
    aLen   = s->length;
  }
  
  if (bVal.type == VAL_SHORT_STRING) {
    bLen = SSO_LENGTH(bVal);
    memcpy(bBuf, bVal.as.shortStr, bLen);
    bBuf[bLen] = '\0';
    bChars = bBuf;
  } else {
    ObjString* s = (ObjString*)AS_OBJ(bVal);
    bChars = s->chars;
    bLen   = s->length;
  }
  
  int length = aLen + bLen;
  char* chars = ALLOCATE(char, length + 1);
  memcpy(chars, aChars, aLen);
  memcpy(chars + aLen, bChars, bLen);
  chars[length] = '\0';
  pop(); pop();
  if (length <= SSO_MAX) {
    push(makeShortString(chars, length));
    FREE_ARRAY(char, chars, length + 1);
  } else {
    ObjString* result = takeString(chars, length);
    push(OBJ_VAL(result));
  }
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
  do{if(!IS_NUMBER(peek(0))||!IS_NUMBER(peek(1))){SAVE_IP();runtimeError("Operands must be numbers.");return INTERPRET_RUNTIME_ERROR;}\
    double b=AS_NUMBER(pop()),a=AS_NUMBER(pop());push(vt(a op b));}while(false)

  for(;;){
    switch(READ_BYTE()){
      case OP_CONSTANT:push(READ_CONSTANT());break;
      case OP_NIL:push(NIL_VAL);break;
      case OP_TRUE:push(BOOL_VAL(true));break;
      case OP_FALSE:push(BOOL_VAL(false));break;
      case OP_POP:pop();break;
      case OP_GET_LOCAL:{uint8_t slot=READ_BYTE();push(frame->slots[slot]);break;}
      case OP_SET_LOCAL:{uint8_t slot=READ_BYTE();frame->slots[slot]=peek(0);break;}
      case OP_GET_GLOBAL:{ObjString* name=READ_STRING();Value v;if(!tableGet(&vm.globals,name,&v)){SAVE_IP();runtimeError("Undefined variable '%s'.",name->chars);return INTERPRET_RUNTIME_ERROR;}push(v);break;}
      case OP_DEFINE_GLOBAL:{ObjString* name=READ_STRING();tableSet(&vm.globals,name,peek(0));pop();break;}
      case OP_SET_GLOBAL:{ObjString* name=READ_STRING();if(tableSet(&vm.globals,name,peek(0))){tableDelete(&vm.globals,name);SAVE_IP();runtimeError("Undefined variable '%s'.",name->chars);return INTERPRET_RUNTIME_ERROR;}break;}
      case OP_GET_UPVALUE:{uint8_t slot=READ_BYTE();push(*frame->closure->upvalues[slot]->location);break;}
      case OP_SET_UPVALUE:{uint8_t slot=READ_BYTE();*frame->closure->upvalues[slot]->location=peek(0);break;}
      case OP_CLOSE_UPVALUE:closeUpvalues(vm.stackTop-1);pop();break;
      case OP_GET_PROPERTY:{
        if(!IS_INSTANCE(peek(0))){SAVE_IP();runtimeError("Only instances have properties.");return INTERPRET_RUNTIME_ERROR;}
        ObjInstance* instance=AS_INSTANCE(peek(0));ObjString* name=READ_STRING();
        Value value;
        if(tableGet(&instance->fields,name,&value)){pop();push(value);break;}
        if(!bindMethod(instance->klass,name)){SAVE_IP();return INTERPRET_RUNTIME_ERROR;}
        break;
      }
      case OP_SET_PROPERTY:{
        if(!IS_INSTANCE(peek(1))){SAVE_IP();runtimeError("Only instances have fields.");return INTERPRET_RUNTIME_ERROR;}
        ObjInstance* instance=AS_INSTANCE(peek(1));tableSet(&instance->fields,READ_STRING(),peek(0));
        Value value=pop();pop();push(value);break;
      }
      case OP_GET_FIELD_DYNAMIC:{
        if(!IS_STRING(peek(0))){SAVE_IP();runtimeError("Field name must be a string.");return INTERPRET_RUNTIME_ERROR;}
        if(!IS_INSTANCE(peek(1))){SAVE_IP();runtimeError("Only instances have fields.");return INTERPRET_RUNTIME_ERROR;}
        ObjString* name=AS_STRING(pop());ObjInstance* instance=AS_INSTANCE(pop());
        Value value;if(tableGet(&instance->fields,name,&value)){push(value);}else{push(NIL_VAL);}break;
      }
      case OP_SET_FIELD_DYNAMIC:{
        Value new_val=pop();
        if(!IS_STRING(peek(0))){SAVE_IP();runtimeError("Field name must be a string.");return INTERPRET_RUNTIME_ERROR;}
        if(!IS_INSTANCE(peek(1))){SAVE_IP();runtimeError("Only instances have fields.");return INTERPRET_RUNTIME_ERROR;}
        ObjString* name=AS_STRING(pop());ObjInstance* instance=AS_INSTANCE(pop());tableSet(&instance->fields,name,new_val);push(new_val);break;
      }
      case OP_DELETE_FIELD:{
        if(!IS_INSTANCE(peek(0))){SAVE_IP();runtimeError("Only instances have fields.");return INTERPRET_RUNTIME_ERROR;}
        ObjInstance* instance=AS_INSTANCE(pop());ObjString* name=READ_STRING();tableDelete(&instance->fields,name);push(NIL_VAL);pop();break;
      }
      case OP_GET_SUPER:{
        ObjString* name=READ_STRING();ObjClass* superclass=AS_CLASS(pop());
        if(!bindMethod(superclass,name)){SAVE_IP();return INTERPRET_RUNTIME_ERROR;}break;
      }
      case OP_EQUAL:{Value b=pop(),a=pop();push(BOOL_VAL(valuesEqual(a,b)));break;}
      case OP_GREATER:BINARY_OP(BOOL_VAL,>);break;
      case OP_LESS:BINARY_OP(BOOL_VAL,<);break;
      case OP_ADD:
        if(isStrVal(peek(0))&&isStrVal(peek(1)))concatenate();
        else if(IS_NUMBER(peek(0))&&IS_NUMBER(peek(1))){double b=AS_NUMBER(pop()),a=AS_NUMBER(pop());push(NUMBER_VAL(a+b));}
        else{SAVE_IP();runtimeError("Operands must be two numbers or two strings.");return INTERPRET_RUNTIME_ERROR;}
        break;
      case OP_SUBTRACT:BINARY_OP(NUMBER_VAL,-);break;
      case OP_MULTIPLY:BINARY_OP(NUMBER_VAL,*);break;
      case OP_DIVIDE:BINARY_OP(NUMBER_VAL,/);break;
      case OP_NOT:push(BOOL_VAL(isFalsey(pop())));break;
      case OP_NEGATE:
        if(!IS_NUMBER(peek(0))){SAVE_IP();runtimeError("Operand must be a number.");return INTERPRET_RUNTIME_ERROR;}
        *(vm.stackTop-1)=NUMBER_VAL(-AS_NUMBER(*(vm.stackTop-1)));break;
      case OP_PRINT:printValue(pop());printf("\n");break;
      case OP_JUMP:{uint16_t off=READ_SHORT();ip+=off;break;}
      case OP_JUMP_IF_FALSE:{uint16_t off=READ_SHORT();if(isFalsey(peek(0)))ip+=off;break;}
      case OP_LOOP:{uint16_t off=READ_SHORT();ip-=off;break;}
      case OP_CALL:{
        int argc=READ_BYTE();SAVE_IP();uint8_t* new_ip=ip;
        if(!callValue(peek(argc),argc,&new_ip))return INTERPRET_RUNTIME_ERROR;
        ip=new_ip;frame=&vm.frames[vm.frameCount-1];break;
      }
      case OP_INVOKE:{
        ObjString* method=READ_STRING();int argCount=READ_BYTE();
        SAVE_IP();uint8_t* new_ip=ip;
        if(!invoke(method,argCount,&new_ip))return INTERPRET_RUNTIME_ERROR;
        ip=new_ip;frame=&vm.frames[vm.frameCount-1];break;
      }
      case OP_SUPER_INVOKE:{
        ObjString* method=READ_STRING();int argCount=READ_BYTE();
        ObjClass* superclass=AS_CLASS(pop());
        SAVE_IP();uint8_t* new_ip=ip;
        if(!invokeFromClass(superclass,method,argCount,&new_ip))return INTERPRET_RUNTIME_ERROR;
        ip=new_ip;frame=&vm.frames[vm.frameCount-1];break;
      }
      case OP_CLOSURE:{
        ObjFunction* fn=AS_FUNCTION(READ_CONSTANT());ObjClosure* closure=newClosure(fn);push(OBJ_VAL(closure));
        for(int i=0;i<closure->upvalueCount;i++){uint8_t isLocal=READ_BYTE();uint8_t index=READ_BYTE();if(isLocal)closure->upvalues[i]=captureUpvalue(frame->slots+index);else closure->upvalues[i]=frame->closure->upvalues[index];}
        break;
      }
      case OP_CLASS:push(OBJ_VAL(newClass(READ_STRING())));break;

      /* ch29: OP_INHERIT — copy-down + store superclass pointer
         We use standard copy-down (tableAddAll) for all methods,
         EXCEPT for BETA challenge: when OP_INVOKE is called, we
         intercept at runtime via invokeTopOfChain to call the
         highest ancestor's method.  The copy-down still happens
         so inherited-but-not-overridden methods (like init) work. */
      case OP_INHERIT:{
        Value superclass=peek(1);
        if(!IS_CLASS(superclass)){SAVE_IP();runtimeError("Superclass must be a class.");return INTERPRET_RUNTIME_ERROR;}
        ObjClass* subclass=AS_CLASS(peek(0));
        ObjClass* super=AS_CLASS(superclass);
        /* Copy-down: subclass gets all superclass methods as defaults */
        tableAddAll(&super->methods,&subclass->methods);
        subclass->superclass=super;
        if(subclass->initMethod==NULL&&super->initMethod!=NULL)subclass->initMethod=super->initMethod;
        pop();break;
      }

      case OP_METHOD:{
        ObjString* name=READ_STRING();Value method=peek(0);ObjClass* klass=AS_CLASS(peek(1));
        tableSet(&klass->methods,name,method);
        if(name->length==4&&memcmp(name->chars,"init",4)==0)klass->initMethod=AS_CLOSURE(method);
        pop();break;
      }

      /* ----------------------------------------------------------------
         Challenge 3: OP_INNER — BETA inner() dispatch

         When OP_INNER executes, we are inside the method of some class C
         (the "definer"). The currently executing closure IS the method.
         We need to find the next subclass below C in the chain from
         instance->klass down to C, and call the same method name there.

         Algorithm (O(depth), depth is typically < 10):
         1. Get 'this' from frame->slots[0].
         2. Get the currently executing closure from frame->closure.
         3. Walk instance->klass → superclass → ... collecting chain[].
         4. Find index `definerIdx` where chain[i]->methods[name] == frame->closure.
         5. The "next subclass" is chain[definerIdx - 1] (one step toward concrete).
         6. If that class has the method, call it.  Otherwise no-op.

         Performance: one chain walk (O(d)) + one table lookup.
         For typical class hierarchies (d <= 5), this is very fast.
         A cache of (closure → subclass) pairs could make it O(1)
         but is unnecessary given shallow hierarchies in practice.
      ---------------------------------------------------------------- */
      case OP_INNER:{
        int argCount=READ_BYTE();
        /* Get 'this' */
        Value receiver=frame->slots[0];
        if(!IS_INSTANCE(receiver)){SAVE_IP();runtimeError("inner() requires a class instance.");return INTERPRET_RUNTIME_ERROR;}
        ObjInstance* instance=AS_INSTANCE(receiver);
        ObjClosure* currentMethod=frame->closure;
        ObjString* methodName=currentMethod->function->name;
        if(!methodName){SAVE_IP();runtimeError("inner() in non-method context.");return INTERPRET_RUNTIME_ERROR;}

        /* Build chain from concrete class up to root */
        ObjClass* chain[64]; int chainLen=0;
        for(ObjClass* k=instance->klass; k!=NULL&&chainLen<64; k=k->superclass)
          chain[chainLen++]=k;
        /* chain[0] = concrete class, chain[chainLen-1] = root */

        /* Find definer: the class whose method table maps methodName → currentMethod */
        int definerIdx=-1;
        for(int i=0;i<chainLen;i++){
          Value m;
          if(tableGet(&chain[i]->methods,methodName,&m)&&IS_CLOSURE(m)&&AS_CLOSURE(m)==currentMethod){
            definerIdx=i; break;
          }
        }

        if(definerIdx<=0){
          /* No subclass below definer (or definer not found): inner() no-op */
          vm.stackTop-=argCount;  /* discard arguments */
          push(NIL_VAL);
          break;
        }

        /* Next subclass is chain[definerIdx - 1] */
        ObjClass* subclass=chain[definerIdx-1];
        Value subMethod;
        if(!tableGet(&subclass->methods,methodName,&subMethod)||!IS_CLOSURE(subMethod)){
          /* Subclass doesn't define this method: no-op */
          vm.stackTop-=argCount;
          push(NIL_VAL);
          break;
        }

        /* Call the subclass method.
           Stack currently: [ ... | arg0 | arg1 | ... | argN ]  (argCount args on top)
           We need:         [ receiver | arg0 | ... | argN ]
           Insert receiver below args. */
        /* Make room for receiver */
        push(NIL_VAL);  /* extend stack by 1 */
        /* Shift args up by 1 */
        for(int i=0;i<argCount;i++)
          vm.stackTop[-1-i]=vm.stackTop[-2-i];
        /* Place receiver in the slot just below args */
        vm.stackTop[-1-argCount]=receiver;

        SAVE_IP();uint8_t* new_ip=ip;
        if(!call(AS_CLOSURE(subMethod),argCount,&new_ip))return INTERPRET_RUNTIME_ERROR;
        ip=new_ip;frame=&vm.frames[vm.frameCount-1];
        break;
      }

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

static void defineNative(const char* name,NativeFn fn,int arity){push(OBJ_VAL(copyString(name,(int)strlen(name))));push(OBJ_VAL(newNative(fn,arity)));tableSet(&vm.globals,AS_STRING(vm.stack[0]),vm.stack[1]);pop();pop();}
static NativeResult n_clock(int c,Value* a){(void)c;(void)a;return nativeOk(NUMBER_VAL((double)clock()/CLOCKS_PER_SEC));}
static NativeResult n_sqrt(int c,Value* a){(void)c;if(!IS_NUMBER(a[0]))return nativeError("sqrt() requires a number.");double n=AS_NUMBER(a[0]);if(n<0)return nativeError("sqrt() of negative.");return nativeOk(NUMBER_VAL(sqrt(n)));}
static NativeResult n_str(int c,Value* a){(void)c;char buf[64];if(IS_STRING(a[0]))return nativeOk(a[0]);if(IS_NUMBER(a[0])){int len=snprintf(buf,sizeof(buf),"%g",AS_NUMBER(a[0]));return nativeOk(OBJ_VAL(copyString(buf,len)));}if(IS_BOOL(a[0])){const char* b=AS_BOOL(a[0])?"true":"false";return nativeOk(OBJ_VAL(copyString(b,(int)strlen(b))));}return nativeOk(OBJ_VAL(copyString("nil",3)));}
static NativeResult n_type(int c,Value* a){(void)c;const char* t;if(IS_BOOL(a[0]))t="bool";else if(IS_NIL(a[0]))t="nil";else if(IS_NUMBER(a[0]))t="number";else if(IS_STRING(a[0]))t="string";else if(IS_CLASS(a[0]))t="class";else if(IS_INSTANCE(a[0]))t="instance";else if(IS_CLOSURE(a[0])||IS_FUNCTION(a[0]))t="function";else t="native";return nativeOk(OBJ_VAL(copyString(t,(int)strlen(t))));}
void registerNatives(void){defineNative("clock",n_clock,0);defineNative("sqrt",n_sqrt,1);defineNative("str",n_str,1);defineNative("type",n_type,1);}
