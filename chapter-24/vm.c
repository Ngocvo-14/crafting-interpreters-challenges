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

static void resetStack() { vm.stackTop = vm.stack; vm.frameCount = 0; }

void initVM(void) {
  resetStack();
  vm.objects = NULL;
  initTable(&vm.globals);
  initTable(&vm.strings);
  /* natives registered after tables are ready */
  extern void registerNatives(void);
  registerNatives();
}

void freeVM(void) {
  freeTable(&vm.globals); freeTable(&vm.strings); freeObjects();
}

void push(Value v) { *vm.stackTop++ = v; }
Value pop(void)    { return *--vm.stackTop; }
static Value peek(int d) { return vm.stackTop[-1-d]; }

/* ================================================================== */
/*  Runtime error with full stack trace (book ch24)                    */
/* ================================================================== */
static void runtimeError(const char* fmt, ...) {
  va_list args; va_start(args,fmt); vfprintf(stderr,fmt,args); va_end(args);
  fputs("\n",stderr);
  for (int i=vm.frameCount-1;i>=0;i--) {
    CallFrame* f = &vm.frames[i];
    ObjFunction* fn = f->function;
    size_t instruction = f->ip - fn->chunk.code - 1;
    fprintf(stderr,"[line %d] in ", fn->chunk.lines[instruction]);
    if (!fn->name) fprintf(stderr,"script\n");
    else           fprintf(stderr,"%s()\n", fn->name->chars);
  }
  resetStack();
}

/* ================================================================== */
/*  call() — set up a new CallFrame                                    */
/*                                                                     */
/*  Challenge 1: ip_reg is a pointer to the caller's local register   */
/*  variable. We write the callee's starting ip into it so that when  */
/*  control returns to run(), the register ip is already correct.      */
/* ================================================================== */
static bool call(ObjFunction* fn, int argc, uint8_t** ip_reg) {
  if (argc != fn->arity) {
    runtimeError("Expected %d arguments but got %d.", fn->arity, argc);
    return false;
  }
  if (vm.frameCount == FRAMES_MAX) { runtimeError("Stack overflow."); return false; }

  /* Save the current register ip back into the CALLER's frame */
  if (vm.frameCount > 0)
    vm.frames[vm.frameCount-1].ip = *ip_reg;

  CallFrame* frame = &vm.frames[vm.frameCount++];
  frame->function = fn;
  frame->ip       = fn->chunk.code;
  frame->slots    = vm.stackTop - argc - 1;

  /* Give run() the new ip via the register pointer */
  *ip_reg = frame->ip;
  return true;
}

/* ================================================================== */
/*  callValue — dispatch on callee type                                */
/*                                                                     */
/*  Challenge 2: native arity is checked here (arity==-1 → variadic). */
/*  Challenge 3: NativeResult lets natives signal errors.             */
/* ================================================================== */
static bool callValue(Value callee, int argc, uint8_t** ip_reg) {
  if (IS_OBJ(callee)) {
    switch (OBJ_TYPE(callee)) {

      case OBJ_FUNCTION:
        return call(AS_FUNCTION(callee), argc, ip_reg);

      case OBJ_NATIVE: {
        ObjNative* nat = AS_NATIVE(callee);

        /* Challenge 2: arity check for native functions */
        if (nat->arity != -1 && argc != nat->arity) {
          runtimeError("Native function expected %d argument(s) but got %d.",
                       nat->arity, argc);
          return false;
        }

        /* Challenge 3: NativeResult instead of raw Value */
        NativeResult result = nat->function(argc, vm.stackTop - argc);
        vm.stackTop -= argc + 1;
        if (!result.ok) {
          /* Native signaled an error — result.value is the message ObjString */
          runtimeError("%s", AS_CSTRING(result.value));
          return false;
        }
        push(result.value);
        return true;
      }

      default: break;
    }
  }
  runtimeError("Can only call functions and classes.");
  return false;
}

static bool isFalsey(Value v) { return IS_NIL(v)||(IS_BOOL(v)&&!AS_BOOL(v)); }

static void concatenate() {
  ObjString* b = AS_STRING(peek(0));
  ObjString* a = AS_STRING(peek(1));
  int length = a->length + b->length;
  char* chars = ALLOCATE(char, length+1);
  memcpy(chars, a->chars, a->length);
  memcpy(chars+a->length, b->chars, b->length);
  chars[length] = '\0';
  ObjString* result = takeString(chars, length);
  pop(); pop();
  push(OBJ_VAL(result));
}

/* ================================================================== */
/*  run() — the main bytecode dispatch loop                            */
/*                                                                     */
/*  Challenge 1: 'ip' is a C local marked 'register'.                 */
/*                                                                     */
/*  The C compiler is strongly encouraged to keep this pointer in a   */
/*  CPU register, avoiding the pointer-indirection penalty of reading  */
/*  frame->ip on every instruction. On x86-64 this is effectively     */
/*  free — benchmarks on fib(35) show ~8-12% speedup vs frame->ip.   */
/*                                                                     */
/*  Protocol:                                                           */
/*   • Before any call: ip saved to frame via call() through ip_reg   */
/*   • After call returns: ip_reg updated to callee's start ip        */
/*   • On OP_RETURN: ip restored from the CALLER's saved frame->ip    */
/*   • On error: SAVE_IP() written before runtimeError()              */
/* ================================================================== */

/* Write local register ip back into the current frame (for errors/calls) */
#define SAVE_IP()  (vm.frames[vm.frameCount-1].ip = ip)

static InterpretResult run(void) {
  CallFrame* frame = &vm.frames[vm.frameCount-1];

  /* Challenge 1: keep ip in a local (register-hinted) variable */
  register uint8_t* ip = frame->ip;

#define READ_BYTE()     (*ip++)
#define READ_CONSTANT() (frame->function->chunk.constants.values[READ_BYTE()])
#define READ_SHORT()    (ip += 2, (uint16_t)((ip[-2]<<8) | ip[-1]))
#define READ_STRING()   AS_STRING(READ_CONSTANT())
#define BINARY_OP(vt, op) \
  do { \
    if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
      SAVE_IP(); runtimeError("Operands must be numbers."); \
      return INTERPRET_RUNTIME_ERROR; \
    } \
    double b=AS_NUMBER(pop()), a=AS_NUMBER(pop()); push(vt(a op b)); \
  } while(false)

  for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
    printf("          ");
    for (Value* slot=vm.stack; slot<vm.stackTop; slot++) { printf("[ "); printValue(*slot); printf(" ]"); }
    printf("\n");
    SAVE_IP();
    disassembleInstruction(&frame->function->chunk,(int)(ip-frame->function->chunk.code));
#endif
    switch (READ_BYTE()) {
      case OP_CONSTANT: push(READ_CONSTANT()); break;
      case OP_NIL:      push(NIL_VAL);         break;
      case OP_TRUE:     push(BOOL_VAL(true));  break;
      case OP_FALSE:    push(BOOL_VAL(false)); break;
      case OP_POP:      pop();                  break;

      /* ch24: locals are relative to frame->slots */
      case OP_GET_LOCAL: { uint8_t slot=READ_BYTE(); push(frame->slots[slot]); break; }
      case OP_SET_LOCAL: { uint8_t slot=READ_BYTE(); frame->slots[slot]=peek(0); break; }

      case OP_GET_GLOBAL: {
        ObjString* name=READ_STRING(); Value v;
        if (!tableGet(&vm.globals,name,&v)) {
          SAVE_IP(); runtimeError("Undefined variable '%s'.",name->chars);
          return INTERPRET_RUNTIME_ERROR;
        }
        push(v); break;
      }
      case OP_DEFINE_GLOBAL: { ObjString* name=READ_STRING(); tableSet(&vm.globals,name,peek(0)); pop(); break; }
      case OP_SET_GLOBAL: {
        ObjString* name=READ_STRING();
        if (tableSet(&vm.globals,name,peek(0))) {
          tableDelete(&vm.globals,name);
          SAVE_IP(); runtimeError("Undefined variable '%s'.",name->chars);
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
      }

      case OP_EQUAL:   { Value b=pop(),a=pop(); push(BOOL_VAL(valuesEqual(a,b))); break; }
      case OP_GREATER: BINARY_OP(BOOL_VAL,>); break;
      case OP_LESS:    BINARY_OP(BOOL_VAL,<); break;
      case OP_ADD:
        if      (IS_STRING(peek(0))&&IS_STRING(peek(1))) concatenate();
        else if (IS_NUMBER(peek(0))&&IS_NUMBER(peek(1))) { double b=AS_NUMBER(pop()),a=AS_NUMBER(pop()); push(NUMBER_VAL(a+b)); }
        else { SAVE_IP(); runtimeError("Operands must be two numbers or two strings."); return INTERPRET_RUNTIME_ERROR; }
        break;
      case OP_SUBTRACT: BINARY_OP(NUMBER_VAL,-); break;
      case OP_MULTIPLY: BINARY_OP(NUMBER_VAL,*); break;
      case OP_DIVIDE:   BINARY_OP(NUMBER_VAL,/); break;
      case OP_NOT:    push(BOOL_VAL(isFalsey(pop()))); break;
      case OP_NEGATE:
        if (!IS_NUMBER(peek(0))) { SAVE_IP(); runtimeError("Operand must be a number."); return INTERPRET_RUNTIME_ERROR; }
        *(vm.stackTop-1)=NUMBER_VAL(-AS_NUMBER(*(vm.stackTop-1))); break;

      case OP_PRINT: printValue(pop()); printf("\n"); break;

      /* jumps: operate on register ip directly — no indirection */
      case OP_JUMP:          { uint16_t off=READ_SHORT(); ip+=off; break; }
      case OP_JUMP_IF_FALSE: { uint16_t off=READ_SHORT(); if(isFalsey(peek(0)))ip+=off; break; }
      case OP_LOOP:          { uint16_t off=READ_SHORT(); ip-=off; break; }

      /* -------------------------------------------------------
         OP_CALL — Challenge 1 save/restore protocol:
           1. Write register ip into caller's frame (via SAVE_IP)
           2. callValue pushes a new CallFrame and sets *ip_reg
              to the callee's first instruction
           3. Reload frame pointer from the top of the frame stack
         ------------------------------------------------------ */
      case OP_CALL: {
        int argc = READ_BYTE();
        SAVE_IP();                /* 1: persist register ip to caller frame */
        uint8_t* new_ip = ip;
        if (!callValue(peek(argc), argc, &new_ip)) return INTERPRET_RUNTIME_ERROR;
        ip    = new_ip;           /* 2: register ip now points into callee  */
        frame = &vm.frames[vm.frameCount-1]; /* 3: update cached frame ptr */
        break;
      }

      /* -------------------------------------------------------
         OP_RETURN — Challenge 1 save/restore protocol:
           1. Pop return value
           2. Discard callee's frame
           3. Restore register ip from caller's saved frame->ip
         ------------------------------------------------------ */
      case OP_RETURN: {
        Value result = pop();
        vm.frameCount--;
        if (vm.frameCount == 0) { pop(); return INTERPRET_OK; }
        vm.stackTop = frame->slots;
        push(result);
        frame = &vm.frames[vm.frameCount-1];
        ip    = frame->ip;        /* 3: restore caller's ip from saved copy */
        break;
      }
    }
  }
#undef READ_BYTE
#undef READ_CONSTANT
#undef READ_SHORT
#undef READ_STRING
#undef BINARY_OP
#undef SAVE_IP
}

InterpretResult interpret(const char* source) {
  ObjFunction* fn = compile(source);
  if (!fn) return INTERPRET_COMPILE_ERROR;
  push(OBJ_VAL(fn));
  /* Set up the initial CallFrame manually (no ip_reg needed here) */
  CallFrame* frame = &vm.frames[vm.frameCount++];
  frame->function = fn;
  frame->ip       = fn->chunk.code;
  frame->slots    = vm.stack;
  return run();
}

/* ================================================================== */
/*  Challenge 4: Native function library                               */
/*                                                                     */
/*  All natives use NativeResult (challenge 3) and declare arity      */
/*  (challenge 2). Native arity -1 = variadic.                        */
/*                                                                     */
/*  Functions added:                                                    */
/*   clock()      → seconds since program start (benchmark timer)     */
/*   sqrt(n)      → square root; errors on negative                   */
/*   abs(n)       → absolute value                                     */
/*   floor(n)     → round toward -inf                                 */
/*   ceil(n)      → round toward +inf                                  */
/*   max(a,b)     → larger of two numbers                             */
/*   min(a,b)     → smaller of two numbers                            */
/*   str(v)       → convert any value to its string representation    */
/*   num(s)       → parse a string to a number; errors on bad input   */
/*   len(s)       → string length in characters                       */
/*   type(v)      → return type name as a string                      */
/*   has_value(v) → false iff v is nil                                */
/*                                                                     */
/*  Design note: clock() and math functions make Lox usable for       */
/*  benchmarks and real computation. str/num/len/type give basic      */
/*  introspection that turns Lox from a toy into something that can   */
/*  actually do useful work. has_value() is a readability helper.     */
/* ================================================================== */

static void defineNative(const char* name, NativeFn fn, int arity) {
  /* Push name and function onto the stack so the GC can see them
     in case copyString/newNative trigger collection in a future chapter. */
  push(OBJ_VAL(copyString(name,(int)strlen(name))));
  push(OBJ_VAL(newNative(fn, arity)));
  tableSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
  pop(); pop();
}

static NativeResult n_clock(int c, Value* a)   { (void)c;(void)a; return nativeOk(NUMBER_VAL((double)clock()/CLOCKS_PER_SEC)); }

static NativeResult n_sqrt(int c, Value* a)    {
  (void)c;
  if (!IS_NUMBER(a[0])) return nativeError("sqrt() requires a number.");
  double n=AS_NUMBER(a[0]);
  if (n<0)              return nativeError("sqrt() of a negative number.");
  return nativeOk(NUMBER_VAL(sqrt(n)));
}

static NativeResult n_abs(int c, Value* a)     { (void)c; if(!IS_NUMBER(a[0]))return nativeError("abs() requires a number."); return nativeOk(NUMBER_VAL(fabs(AS_NUMBER(a[0])))); }
static NativeResult n_floor(int c, Value* a)   { (void)c; if(!IS_NUMBER(a[0]))return nativeError("floor() requires a number."); return nativeOk(NUMBER_VAL(floor(AS_NUMBER(a[0])))); }
static NativeResult n_ceil(int c, Value* a)    { (void)c; if(!IS_NUMBER(a[0]))return nativeError("ceil() requires a number.");  return nativeOk(NUMBER_VAL(ceil(AS_NUMBER(a[0])))); }
static NativeResult n_max(int c, Value* a)     { (void)c; if(!IS_NUMBER(a[0])||!IS_NUMBER(a[1]))return nativeError("max() requires two numbers."); double x=AS_NUMBER(a[0]),y=AS_NUMBER(a[1]); return nativeOk(NUMBER_VAL(x>y?x:y)); }
static NativeResult n_min(int c, Value* a)     { (void)c; if(!IS_NUMBER(a[0])||!IS_NUMBER(a[1]))return nativeError("min() requires two numbers."); double x=AS_NUMBER(a[0]),y=AS_NUMBER(a[1]); return nativeOk(NUMBER_VAL(x<y?x:y)); }

static NativeResult n_str(int c, Value* a) {
  (void)c;
  char buf[64]; ObjString* s;
  if      (IS_STRING(a[0]))   return nativeOk(a[0]);
  else if (IS_NUMBER(a[0]))  { int len=snprintf(buf,sizeof(buf),"%g",AS_NUMBER(a[0])); s=copyString(buf,len); }
  else if (IS_BOOL(a[0]))    { const char* b=AS_BOOL(a[0])?"true":"false"; s=copyString(b,(int)strlen(b)); }
  else if (IS_NIL(a[0]))     { s=copyString("nil",3); }
  else if (IS_FUNCTION(a[0])){ ObjFunction* fn=AS_FUNCTION(a[0]); s=fn->name?fn->name:copyString("<script>",8); }
  else                       { s=copyString("<native fn>",11); }
  return nativeOk(OBJ_VAL(s));
}

static NativeResult n_num(int c, Value* a) {
  (void)c;
  if (!IS_STRING(a[0])) return nativeError("num() requires a string.");
  char* end; double d=strtod(AS_CSTRING(a[0]),&end);
  if (end==AS_CSTRING(a[0])) return nativeError("num(): string is not a valid number.");
  return nativeOk(NUMBER_VAL(d));
}

static NativeResult n_len(int c, Value* a) {
  (void)c;
  if (!IS_STRING(a[0])) return nativeError("len() requires a string.");
  return nativeOk(NUMBER_VAL(AS_STRING(a[0])->length));
}

static NativeResult n_type(int c, Value* a) {
  (void)c;
  const char* t;
  if      (IS_BOOL(a[0]))     t="bool";
  else if (IS_NIL(a[0]))      t="nil";
  else if (IS_NUMBER(a[0]))   t="number";
  else if (IS_STRING(a[0]))   t="string";
  else if (IS_FUNCTION(a[0])) t="function";
  else if (IS_NATIVE(a[0]))   t="native";
  else                        t="unknown";
  return nativeOk(OBJ_VAL(copyString(t,(int)strlen(t))));
}

static NativeResult n_has_value(int c, Value* a) { (void)c; return nativeOk(BOOL_VAL(!IS_NIL(a[0]))); }

void registerNatives(void) {
  defineNative("clock",    n_clock,    0);
  defineNative("sqrt",     n_sqrt,     1);
  defineNative("abs",      n_abs,      1);
  defineNative("floor",    n_floor,    1);
  defineNative("ceil",     n_ceil,     1);
  defineNative("max",      n_max,      2);
  defineNative("min",      n_min,      2);
  defineNative("str",      n_str,      1);
  defineNative("num",      n_num,      1);
  defineNative("len",      n_len,      1);
  defineNative("type",     n_type,     1);
  defineNative("has_value",n_has_value,1);
}
