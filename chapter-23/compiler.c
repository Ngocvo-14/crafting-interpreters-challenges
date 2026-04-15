#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "chunk.h"
#include "compiler.h"
#include "scanner.h"
#include "object.h"
#include "table.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

/* ================================================================== */
/*  Data structures                                                     */
/* ================================================================== */

typedef struct {
  Token name;
  int   depth;
  bool  isConst;
} Local;

#define LOCAL_MAP_SIZE 64
typedef struct { ObjString* key; int slot; } LocalMapEntry;

/* -----------------------------------------------------------
   Challenge 2 (continue) and the book's while/for loops need
   to track the current loop context so continue knows where
   to jump back to and how many locals to pop first.
   ----------------------------------------------------------- */
#define MAX_CONTINUE_PATCHES 64

typedef struct {
  int  loopStart;      /* bytecode offset to jump back to (top of condition,
                          or increment for 'for' loops)                     */
  int  scopeDepthAtLoop; /* scope depth when the loop was entered           */
  /* continue patches: offsets of OP_JUMP instructions emitted by
     'continue' statements that need to be back-patched once we know
     where the increment / loop-back label is.                               */
  int  continuePatches[MAX_CONTINUE_PATCHES];
  int  continuePatchCount;
} LoopContext;

#define MAX_LOOP_DEPTH 64

typedef struct {
  Local         locals[MAX_LOCALS];
  int           localCount;
  int           scopeDepth;
  LocalMapEntry localMap[LOCAL_MAP_SIZE];
  /* Loop stack for continue support */
  LoopContext   loops[MAX_LOOP_DEPTH];
  int           loopDepth;
} Compiler;

/* ---- Parser ---- */
typedef struct {
  Token current;
  Token previous;
  bool  hadError;
  bool  panicMode;
} Parser;

static Parser    parser;
static Compiler* current = NULL;
static Chunk*    compilingChunk;
static Chunk*    currentChunk() { return compilingChunk; }

/* ================================================================== */
/*  localMap helpers (O(1) resolution – ch22 challenge 1)             */
/* ================================================================== */

static void localMapClear(Compiler* c) { memset(c->localMap,0,sizeof(c->localMap)); }

static void localMapSet(Compiler* c, ObjString* name, int slot) {
  uint32_t idx = name->hash & (LOCAL_MAP_SIZE-1);
  for (int i=0;i<LOCAL_MAP_SIZE;i++) {
    uint32_t s=(idx+i)&(LOCAL_MAP_SIZE-1);
    if(!c->localMap[s].key||c->localMap[s].key==name){c->localMap[s].key=name;c->localMap[s].slot=slot;return;}
  }
}
static int localMapGet(Compiler* c, ObjString* name) {
  uint32_t idx=name->hash&(LOCAL_MAP_SIZE-1);
  for(int i=0;i<LOCAL_MAP_SIZE;i++){
    uint32_t s=(idx+i)&(LOCAL_MAP_SIZE-1);
    if(!c->localMap[s].key) return -1;
    if(c->localMap[s].key==name) return c->localMap[s].slot;
  }
  return -1;
}
static void localMapRemove(Compiler* c, ObjString* name) {
  uint32_t idx=name->hash&(LOCAL_MAP_SIZE-1);
  for(int i=0;i<LOCAL_MAP_SIZE;i++){
    uint32_t s=(idx+i)&(LOCAL_MAP_SIZE-1);
    if(!c->localMap[s].key) return;
    if(c->localMap[s].key==name){
      c->localMap[s].key=NULL;
      for(int j=1;j<LOCAL_MAP_SIZE;j++){
        uint32_t ns=(s+j)&(LOCAL_MAP_SIZE-1);
        if(!c->localMap[ns].key) break;
        LocalMapEntry tmp=c->localMap[ns]; c->localMap[ns].key=NULL;
        localMapSet(c,tmp.key,tmp.slot);
      }
      return;
    }
  }
}

/* ================================================================== */
/*  Error helpers                                                       */
/* ================================================================== */

static void errorAt(Token* token, const char* msg) {
  if(parser.panicMode) return;
  parser.panicMode=true;
  fprintf(stderr,"[line %d] Error",token->line);
  if     (token->type==TOKEN_EOF)   fprintf(stderr," at end");
  else if(token->type!=TOKEN_ERROR) fprintf(stderr," at '%.*s'",token->length,token->start);
  fprintf(stderr,": %s\n",msg);
  parser.hadError=true;
}
static void error(const char* msg)          { errorAt(&parser.previous,msg); }
static void errorAtCurrent(const char* msg) { errorAt(&parser.current,msg); }

/* ================================================================== */
/*  Token helpers                                                       */
/* ================================================================== */

static void advance() {
  parser.previous=parser.current;
  for(;;){parser.current=scanToken();if(parser.current.type!=TOKEN_ERROR)break;errorAtCurrent(parser.current.start);}
}
static void consume(TokenType type, const char* msg) { if(parser.current.type==type){advance();return;} errorAtCurrent(msg); }
static bool check(TokenType type)  { return parser.current.type==type; }
static bool match(TokenType type)  { if(!check(type)) return false; advance(); return true; }

/* ================================================================== */
/*  Emit helpers                                                        */
/* ================================================================== */

static void emitByte(uint8_t byte)               { writeChunk(currentChunk(),byte,parser.previous.line); }
static void emitBytes(uint8_t b1, uint8_t b2)    { emitByte(b1); emitByte(b2); }
static void emitReturn()                          { emitByte(OP_RETURN); }

static uint8_t makeConstant(Value value) {
  int c=addConstant(currentChunk(),value);
  if(c>255){error("Too many constants.");return 0;}
  return (uint8_t)c;
}
static void emitConstant(Value value) { emitBytes(OP_CONSTANT,makeConstant(value)); }

/* ---- jump helpers ---- */
static int emitJump(uint8_t instruction) {
  emitByte(instruction); emitByte(0xff); emitByte(0xff);
  return currentChunk()->count-2;
}
static void patchJump(int offset) {
  int jump=currentChunk()->count-offset-2;
  if(jump>UINT16_MAX){error("Too much code to jump over.");}
  currentChunk()->code[offset]  =(jump>>8)&0xff;
  currentChunk()->code[offset+1]= jump    &0xff;
}

/* ---- loop helper (backward jump) ---- */
static void emitLoop(int loopStart) {
  emitByte(OP_LOOP);
  int offset=currentChunk()->count-loopStart+2;
  if(offset>UINT16_MAX) error("Loop body too large.");
  emitByte((offset>>8)&0xff);
  emitByte( offset    &0xff);
}

/* ---- local get/set (narrow vs wide – ch22 challenge 4) ---- */
static void emitGetLocal(int slot) {
  if(slot<=255){emitBytes(OP_GET_LOCAL,(uint8_t)slot);}
  else{emitByte(OP_GET_LOCAL_WIDE);emitByte((uint8_t)((slot>>8)&0xFF));emitByte((uint8_t)(slot&0xFF));}
}
static void emitSetLocal(int slot) {
  if(slot<=255){emitBytes(OP_SET_LOCAL,(uint8_t)slot);}
  else{emitByte(OP_SET_LOCAL_WIDE);emitByte((uint8_t)((slot>>8)&0xFF));emitByte((uint8_t)(slot&0xFF));}
}

static void endCompiler() {
  emitReturn();
#ifdef DEBUG_PRINT_CODE
  if(!parser.hadError) disassembleChunk(currentChunk(),"code");
#endif
}

/* ================================================================== */
/*  Compiler init                                                       */
/* ================================================================== */

static void initCompiler(Compiler* compiler) {
  compiler->localCount=0; compiler->scopeDepth=0;
  compiler->loopDepth=0;
  localMapClear(compiler);
  current=compiler;
}

/* ================================================================== */
/*  Identifier constant deduplication (ch21)                          */
/* ================================================================== */

#define ID_CACHE_SIZE 64
typedef struct { ObjString* key; uint8_t index; bool occupied; } IdCacheEntry;
static IdCacheEntry idCache[ID_CACHE_SIZE];
static int idCacheCount=0;
static void resetIdCache(){ memset(idCache,0,sizeof(idCache)); idCacheCount=0; }

static uint8_t identifierConstant(Token* name) {
  ObjString* str=copyString(name->start,name->length);
  uint32_t hash=str->hash, idx=hash&(ID_CACHE_SIZE-1);
  for(int i=0;i<ID_CACHE_SIZE;i++){
    uint32_t slot=(idx+i)&(ID_CACHE_SIZE-1);
    IdCacheEntry* e=&idCache[slot];
    if(!e->occupied) break;
    if(e->key==str) return e->index;
  }
  uint8_t constIdx=makeConstant(OBJ_VAL(str));
  if(idCacheCount<ID_CACHE_SIZE){
    uint32_t slot=hash&(ID_CACHE_SIZE-1);
    for(int i=0;i<ID_CACHE_SIZE;i++){
      uint32_t s=(slot+i)&(ID_CACHE_SIZE-1);
      if(!idCache[s].occupied){idCache[s].key=str;idCache[s].index=constIdx;idCache[s].occupied=true;idCacheCount++;break;}
    }
  }
  return constIdx;
}
static bool identifiersEqual(Token* a, Token* b) {
  if(a->length!=b->length) return false;
  return memcmp(a->start,b->start,a->length)==0;
}

/* ================================================================== */
/*  Scope management                                                    */
/* ================================================================== */

static void beginScope() { current->scopeDepth++; }

static void endScope() {
  current->scopeDepth--;
  while(current->localCount>0 &&
        current->locals[current->localCount-1].depth>current->scopeDepth){
    Local* l=&current->locals[current->localCount-1];
    ObjString* nameStr=copyString(l->name.start,l->name.length);
    localMapRemove(current,nameStr);
    emitByte(OP_POP);
    current->localCount--;
  }
}

/* Pop locals down to a target scope depth without actually changing scopeDepth.
   Used by 'continue' to clean up locals declared inside the loop body. */
static void popLocalsToDepth(int targetDepth) {
  int count=current->localCount;
  while(count>0 && current->locals[count-1].depth>targetDepth) {
    emitByte(OP_POP);
    count--;
  }
}

/* ================================================================== */
/*  Local variable declaration & resolution                            */
/* ================================================================== */

static void addLocal(Token name, bool isConst) {
  if(current->localCount==UINT16_COUNT){error("Too many local variables in function.");return;}
  Local* local=&current->locals[current->localCount++];
  local->name=name; local->depth=-1; local->isConst=isConst;
}
static void markInitialized() {
  int idx=current->localCount-1;
  current->locals[idx].depth=current->scopeDepth;
  ObjString* nameStr=copyString(current->locals[idx].name.start,current->locals[idx].name.length);
  localMapSet(current,nameStr,idx);
}
static void declareVariable(bool isConst) {
  if(current->scopeDepth==0) return;
  Token* name=&parser.previous;
  for(int i=current->localCount-1;i>=0;i--){
    Local* local=&current->locals[i];
    if(local->depth!=-1&&local->depth<current->scopeDepth) break;
    if(identifiersEqual(name,&local->name)){error("Already a variable with this name in this scope.");}
  }
  addLocal(*name,isConst);
}
static int resolveLocal(Compiler* compiler, Token* name) {
  ObjString* nameStr=copyString(name->start,name->length);
  int slot=localMapGet(compiler,nameStr);
  if(slot!=-1){
    if(compiler->locals[slot].depth==-1) error("Can't read local variable in its own initializer.");
    return slot;
  }
  for(int i=compiler->localCount-1;i>=0;i--){
    Local* local=&compiler->locals[i];
    if(identifiersEqual(name,&local->name)){
      if(local->depth==-1) error("Can't read local variable in its own initializer.");
      return i;
    }
  }
  return -1;
}

/* ================================================================== */
/*  Forward declarations                                               */
/* ================================================================== */

typedef enum {
  PREC_NONE,PREC_ASSIGNMENT,PREC_OR,PREC_AND,PREC_EQUALITY,
  PREC_COMPARISON,PREC_TERM,PREC_FACTOR,PREC_UNARY,PREC_CALL,PREC_PRIMARY
} Precedence;
typedef void (*ParseFn)(bool canAssign);
typedef struct { ParseFn prefix; ParseFn infix; Precedence precedence; } ParseRule;

static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);
static void expression(void);
static void statement(void);
static void declaration(void);

/* ================================================================== */
/*  Expression parse functions                                         */
/* ================================================================== */

static void number(bool ca)  { (void)ca; emitConstant(NUMBER_VAL(strtod(parser.previous.start,NULL))); }
static void string(bool ca)  { (void)ca; emitConstant(OBJ_VAL(constString(parser.previous.start+1,parser.previous.length-2))); }
static void literal(bool ca) {
  (void)ca;
  switch(parser.previous.type){
    case TOKEN_FALSE: emitByte(OP_FALSE); break;
    case TOKEN_NIL:   emitByte(OP_NIL);   break;
    case TOKEN_TRUE:  emitByte(OP_TRUE);  break;
    default: return;
  }
}
static void grouping(bool ca) { (void)ca; expression(); consume(TOKEN_RIGHT_PAREN,"Expect ')' after expression."); }
static void unary(bool ca) {
  (void)ca;
  TokenType op=parser.previous.type;
  parsePrecedence(PREC_UNARY);
  switch(op){ case TOKEN_BANG: emitByte(OP_NOT); break; case TOKEN_MINUS: emitByte(OP_NEGATE); break; default: return; }
}
static void binary(bool ca) {
  (void)ca;
  TokenType op=parser.previous.type;
  ParseRule* rule=getRule(op);
  parsePrecedence((Precedence)(rule->precedence+1));
  switch(op){
    case TOKEN_BANG_EQUAL:    emitBytes(OP_EQUAL,OP_NOT); break;
    case TOKEN_EQUAL_EQUAL:   emitByte(OP_EQUAL);         break;
    case TOKEN_GREATER:       emitByte(OP_GREATER);       break;
    case TOKEN_GREATER_EQUAL: emitBytes(OP_LESS,OP_NOT);  break;
    case TOKEN_LESS:          emitByte(OP_LESS);           break;
    case TOKEN_LESS_EQUAL:    emitBytes(OP_GREATER,OP_NOT); break;
    case TOKEN_PLUS:          emitByte(OP_ADD);            break;
    case TOKEN_MINUS:         emitByte(OP_SUBTRACT);       break;
    case TOKEN_STAR:          emitByte(OP_MULTIPLY);       break;
    case TOKEN_SLASH:         emitByte(OP_DIVIDE);         break;
    default: return;
  }
}
static void and_(bool ca) {
  (void)ca;
  int endJump=emitJump(OP_JUMP_IF_FALSE);
  emitByte(OP_POP);
  parsePrecedence(PREC_AND);
  patchJump(endJump);
}
static void or_(bool ca) {
  (void)ca;
  int elseJump=emitJump(OP_JUMP_IF_FALSE);
  int endJump =emitJump(OP_JUMP);
  patchJump(elseJump);
  emitByte(OP_POP);
  parsePrecedence(PREC_OR);
  patchJump(endJump);
}

static void namedVariable(Token name, bool canAssign) {
  int arg=resolveLocal(current,&name);
  if(arg!=-1){
    if(canAssign&&match(TOKEN_EQUAL)){
      if(current->locals[arg].isConst) error("Cannot assign to a 'val' variable.");
      expression(); emitSetLocal(arg);
    } else { emitGetLocal(arg); }
  } else {
    uint8_t idx=identifierConstant(&name);
    if(canAssign&&match(TOKEN_EQUAL)){ expression(); emitBytes(OP_SET_GLOBAL,idx); }
    else { emitBytes(OP_GET_GLOBAL,idx); }
  }
}
static void variable(bool ca) { namedVariable(parser.previous,ca); }

/* ================================================================== */
/*  Parse table                                                        */
/* ================================================================== */

static ParseRule rules[] = {
  [TOKEN_LEFT_PAREN]    = {grouping, NULL,   PREC_NONE},
  [TOKEN_RIGHT_PAREN]   = {NULL,     NULL,   PREC_NONE},
  [TOKEN_LEFT_BRACE]    = {NULL,     NULL,   PREC_NONE},
  [TOKEN_RIGHT_BRACE]   = {NULL,     NULL,   PREC_NONE},
  [TOKEN_COLON]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_COMMA]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_DOT]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_MINUS]         = {unary,    binary, PREC_TERM},
  [TOKEN_PLUS]          = {NULL,     binary, PREC_TERM},
  [TOKEN_SEMICOLON]     = {NULL,     NULL,   PREC_NONE},
  [TOKEN_SLASH]         = {NULL,     binary, PREC_FACTOR},
  [TOKEN_STAR]          = {NULL,     binary, PREC_FACTOR},
  [TOKEN_BANG]          = {unary,    NULL,   PREC_NONE},
  [TOKEN_BANG_EQUAL]    = {NULL,     binary, PREC_EQUALITY},
  [TOKEN_EQUAL]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_EQUAL_EQUAL]   = {NULL,     binary, PREC_EQUALITY},
  [TOKEN_GREATER]       = {NULL,     binary, PREC_COMPARISON},
  [TOKEN_GREATER_EQUAL] = {NULL,     binary, PREC_COMPARISON},
  [TOKEN_LESS]          = {NULL,     binary, PREC_COMPARISON},
  [TOKEN_LESS_EQUAL]    = {NULL,     binary, PREC_COMPARISON},
  [TOKEN_IDENTIFIER]    = {variable, NULL,   PREC_NONE},
  [TOKEN_STRING]        = {string,   NULL,   PREC_NONE},
  [TOKEN_NUMBER]        = {number,   NULL,   PREC_NONE},
  [TOKEN_AND]           = {NULL,     and_,   PREC_AND},
  [TOKEN_CLASS]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_ELSE]          = {NULL,     NULL,   PREC_NONE},
  [TOKEN_FALSE]         = {literal,  NULL,   PREC_NONE},
  [TOKEN_FOR]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_FUN]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_IF]            = {NULL,     NULL,   PREC_NONE},
  [TOKEN_NIL]           = {literal,  NULL,   PREC_NONE},
  [TOKEN_OR]            = {NULL,     or_,    PREC_OR},
  [TOKEN_PRINT]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_RETURN]        = {NULL,     NULL,   PREC_NONE},
  [TOKEN_SUPER]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_THIS]          = {NULL,     NULL,   PREC_NONE},
  [TOKEN_TRUE]          = {literal,  NULL,   PREC_NONE},
  [TOKEN_VAR]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_VAL]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_WHILE]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_SWITCH]        = {NULL,     NULL,   PREC_NONE},
  [TOKEN_CASE]          = {NULL,     NULL,   PREC_NONE},
  [TOKEN_DEFAULT]       = {NULL,     NULL,   PREC_NONE},
  [TOKEN_CONTINUE]      = {NULL,     NULL,   PREC_NONE},
  [TOKEN_REPEAT]        = {NULL,     NULL,   PREC_NONE},
  [TOKEN_UNTIL]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_ERROR]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_EOF]           = {NULL,     NULL,   PREC_NONE},
};

static ParseRule* getRule(TokenType type) { return &rules[type]; }

static void parsePrecedence(Precedence precedence) {
  advance();
  ParseFn prefix=getRule(parser.previous.type)->prefix;
  if(!prefix){error("Expect expression.");return;}
  bool canAssign=precedence<=PREC_ASSIGNMENT;
  prefix(canAssign);
  while(precedence<=getRule(parser.current.type)->precedence){
    advance(); getRule(parser.previous.type)->infix(canAssign);
  }
  if(canAssign&&match(TOKEN_EQUAL)) error("Invalid assignment target.");
}
static void expression(void) { parsePrecedence(PREC_ASSIGNMENT); }

/* ================================================================== */
/*  Statements                                                         */
/* ================================================================== */

static void printStatement(void) {
  expression(); consume(TOKEN_SEMICOLON,"Expect ';' after value."); emitByte(OP_PRINT);
}
static void expressionStatement(void) {
  expression(); consume(TOKEN_SEMICOLON,"Expect ';' after expression."); emitByte(OP_POP);
}
static void block(void) {
  while(!check(TOKEN_RIGHT_BRACE)&&!check(TOKEN_EOF)) declaration();
  consume(TOKEN_RIGHT_BRACE,"Expect '}' after block.");
}

/* ------------------------------------------------------------------ */
/*  if statement (book ch23)                                           */
/* ------------------------------------------------------------------ */
static void ifStatement(void) {
  consume(TOKEN_LEFT_PAREN,"Expect '(' after 'if'.");
  expression();
  consume(TOKEN_RIGHT_PAREN,"Expect ')' after condition.");

  int thenJump=emitJump(OP_JUMP_IF_FALSE);
  emitByte(OP_POP);
  statement();
  int elseJump=emitJump(OP_JUMP);
  patchJump(thenJump);
  emitByte(OP_POP);
  if(match(TOKEN_ELSE)) statement();
  patchJump(elseJump);
}

/* ------------------------------------------------------------------ */
/*  while statement (book ch23)                                        */
/* ------------------------------------------------------------------ */
static void whileStatement(void) {
  /* Push loop context */
  if(current->loopDepth==MAX_LOOP_DEPTH){error("Too many nested loops.");return;}
  LoopContext* lc=&current->loops[current->loopDepth++];
  lc->continuePatchCount=0;
  lc->scopeDepthAtLoop=current->scopeDepth;

  int loopStart=currentChunk()->count;
  lc->loopStart=loopStart;    /* continue jumps back here (before condition) */

  consume(TOKEN_LEFT_PAREN,"Expect '(' after 'while'.");
  expression();
  consume(TOKEN_RIGHT_PAREN,"Expect ')' after condition.");

  int exitJump=emitJump(OP_JUMP_IF_FALSE);
  emitByte(OP_POP);
  statement();

  /* Patch all continue jumps so they land here (= loop-back point) */
  for(int i=0;i<lc->continuePatchCount;i++) patchJump(lc->continuePatches[i]);

  emitLoop(loopStart);
  patchJump(exitJump);
  emitByte(OP_POP);

  current->loopDepth--;
}

/* ------------------------------------------------------------------ */
/*  for statement (book ch23)                                          */
/* ------------------------------------------------------------------ */
static void forStatement(void) {
  if(current->loopDepth==MAX_LOOP_DEPTH){error("Too many nested loops.");return;}
  LoopContext* lc=&current->loops[current->loopDepth++];
  lc->continuePatchCount=0;

  beginScope();
  consume(TOKEN_LEFT_PAREN,"Expect '(' after 'for'.");

  /* initializer */
  if(match(TOKEN_SEMICOLON)){
    /* no initializer */
  } else if(match(TOKEN_VAR)){
    /* forward-declare varDeclaration */
    extern void varDeclaration(bool isConst);
    varDeclaration(false);
  } else {
    expressionStatement();
  }

  lc->scopeDepthAtLoop=current->scopeDepth;

  int loopStart=currentChunk()->count;
  int exitJump=-1;

  /* condition */
  if(!match(TOKEN_SEMICOLON)){
    expression();
    consume(TOKEN_SEMICOLON,"Expect ';' after loop condition.");
    exitJump=emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
  }

  /* increment */
  if(!match(TOKEN_RIGHT_PAREN)){
    int bodyJump=emitJump(OP_JUMP);
    int incrementStart=currentChunk()->count;

    expression();
    emitByte(OP_POP);
    consume(TOKEN_RIGHT_PAREN,"Expect ')' after for clauses.");

    emitLoop(loopStart);
    loopStart=incrementStart;
    patchJump(bodyJump);

    /* continue for a for-loop jumps to the increment */
    lc->loopStart=incrementStart;
  } else {
    /* no increment: continue goes to the condition */
    lc->loopStart=loopStart;
  }

  statement();

  /* Patch continue jumps so they land at loopStart (increment or condition) */
  for(int i=0;i<lc->continuePatchCount;i++) patchJump(lc->continuePatches[i]);

  emitLoop(loopStart);

  if(exitJump!=-1){ patchJump(exitJump); emitByte(OP_POP); }

  endScope();
  current->loopDepth--;
}

/* ------------------------------------------------------------------ */
/*  Challenge 1: switch statement                                      */
/*                                                                     */
/*  Strategy: keep the switch value on the stack at stack[-1].        */
/*  For each case:                                                     */
/*    1. Evaluate the case expression (pushed on top → stack[-1]).    */
/*    2. Emit OP_SWITCH_EQUAL: if stack[-2] == stack[-1] it pops      */
/*       both, then skips the following OP_JUMP (i.e. falls into the  */
/*       case body). If not equal it pops just the case value and     */
/*       lands on the OP_JUMP, which jumps over the body to the next  */
/*       case.                                                         */
/*    3. After the body, emit OP_JUMP to the end of the switch.       */
/*                                                                     */
/*  After all cases, pop the switch value (default or no-match path). */
/* ------------------------------------------------------------------ */

#define MAX_SWITCH_CASES 256

static void switchStatement(void) {
  consume(TOKEN_LEFT_PAREN,"Expect '(' after 'switch'.");
  expression();               /* switch value stays on stack */
  consume(TOKEN_RIGHT_PAREN,"Expect ')' after switch value.");
  consume(TOKEN_LEFT_BRACE,"Expect '{' before switch cases.");

  /* We'll collect "jump to end" patches from each matched case body */
  int endJumps[MAX_SWITCH_CASES];
  int endJumpCount=0;

  /* Process case clauses */
  while(match(TOKEN_CASE)){
    /* Evaluate case expression */
    expression();
    consume(TOKEN_COLON,"Expect ':' after case value.");

    /*
     * OP_SWITCH_EQUAL: if top-1 == top  →  pop both, skip next jump (body)
     *                  else              →  pop top only, take jump (skip body)
     * We emit OP_SWITCH_EQUAL + OP_JUMP over the body (patched after body).
     */
    emitByte(OP_SWITCH_EQUAL);
    int skipBody=emitJump(OP_JUMP);   /* taken when NOT equal — skip body */

    /* --- body --- */
    while(!check(TOKEN_CASE)&&!check(TOKEN_DEFAULT)&&!check(TOKEN_RIGHT_BRACE)&&!check(TOKEN_EOF))
      declaration();

    /* Jump to end of switch after body executes */
    if(endJumpCount<MAX_SWITCH_CASES)
      endJumps[endJumpCount++]=emitJump(OP_JUMP);

    /* The "not equal" skip-body jump lands here */
    patchJump(skipBody);
  }

  /* Optional default clause */
  if(match(TOKEN_DEFAULT)){
    consume(TOKEN_COLON,"Expect ':' after 'default'.");
    while(!check(TOKEN_RIGHT_BRACE)&&!check(TOKEN_EOF)) declaration();
  }

  consume(TOKEN_RIGHT_BRACE,"Expect '}' after switch cases.");

  /* Pop the switch value (reached when no case matched, or after default) */
  emitByte(OP_POP);

  /* Patch all end-of-body jumps */
  for(int i=0;i<endJumpCount;i++) patchJump(endJumps[i]);
}

/* ------------------------------------------------------------------ */
/*  Challenge 2: continue statement                                    */
/*                                                                     */
/*  A continue inside a loop must:                                     */
/*    1. Pop any locals declared inside the loop body since the loop  */
/*       was entered (scope cleanup).                                  */
/*    2. Jump to loopStart (the increment clause for 'for', the       */
/*       condition for 'while'/'repeat-until').                        */
/*                                                                     */
/*  Because we don't yet know the final loopStart when compiling the  */
/*  body (for-increment case), we emit a placeholder OP_JUMP and      */
/*  store it in the LoopContext's continuePatches array. After the     */
/*  body is done, forStatement/whileStatement patches them all.        */
/* ------------------------------------------------------------------ */

static void continueStatement(void) {
  if(current->loopDepth==0){
    error("Cannot use 'continue' outside of a loop.");
    return;
  }
  consume(TOKEN_SEMICOLON,"Expect ';' after 'continue'.");

  LoopContext* lc=&current->loops[current->loopDepth-1];

  /* Pop any locals that were declared inside the loop body */
  popLocalsToDepth(lc->scopeDepthAtLoop);

  /* Emit a forward jump placeholder to be patched later */
  int patch=emitJump(OP_JUMP);
  if(lc->continuePatchCount<MAX_CONTINUE_PATCHES)
    lc->continuePatches[lc->continuePatchCount++]=patch;
  else
    error("Too many 'continue' statements in one loop.");
}

/* ------------------------------------------------------------------ */
/*  Challenge 3: repeat/until loop                                     */
/*                                                                     */
/*  Grammar:                                                           */
/*    repeatStmt → "repeat" statement "until" "(" expression ")" ";" */
/*                                                                     */
/*  Semantics: execute the body at least once, then check the         */
/*  condition. If truthy, stop. If falsey, loop back and repeat.      */
/*  (Inverse of C's do/while: loop while condition is FALSE.)         */
/*                                                                     */
/*  Design note: "repeat ... until (cond)" reads as "repeat the body  */
/*  until the condition becomes true", which is more natural in        */
/*  English than do/while. This idiom comes from Pascal/Ada and avoids */
/*  the confusing double-negative of do { } while(!done).             */
/*                                                                     */
/*  'continue' support: same as while — jumps back to loopStart,      */
/*  which is right before the body (the whole loop repeats, not just   */
/*  skipping to the condition like a C do/while continue does).        */
/*                                                                     */
/*  Bytecode layout:                                                   */
/*    [loopStart]                                                       */
/*    <body>                                                            */
/*    <continue patches land here>                                      */
/*    <condition expression>                                            */
/*    OP_JUMP_IF_FALSE → loopStart   (loop again if condition false)  */
/*    OP_POP                                                            */
/*    [end]                                                             */
/* ------------------------------------------------------------------ */

static void repeatStatement(void) {
  if(current->loopDepth==MAX_LOOP_DEPTH){error("Too many nested loops.");return;}
  LoopContext* lc=&current->loops[current->loopDepth++];
  lc->continuePatchCount=0;
  lc->scopeDepthAtLoop=current->scopeDepth;

  int loopStart=currentChunk()->count;
  lc->loopStart=loopStart;

  statement();   /* body — executed at least once */

  /* Patch continue jumps so they land right before the condition */
  for(int i=0;i<lc->continuePatchCount;i++) patchJump(lc->continuePatches[i]);

  consume(TOKEN_UNTIL,"Expect 'until' after repeat body.");
  consume(TOKEN_LEFT_PAREN,"Expect '(' after 'until'.");
  expression();       /* condition: if TRUE we exit */
  consume(TOKEN_RIGHT_PAREN,"Expect ')' after until condition.");
  consume(TOKEN_SEMICOLON,"Expect ';' after repeat statement.");

  /* If condition is FALSE, loop back */
  int exitJump=emitJump(OP_JUMP_IF_FALSE);
  emitByte(OP_POP);   /* condition was truthy: clean up and fall through */
  /* fall through = exit loop */

  /* patch the false-branch to loop back */
  /* We need OP_LOOP (backward), but exitJump points to the OP_JUMP_IF_FALSE
     that jumps FORWARD when false. We want to loop when false.
     So we swap strategy: emit OP_LOOP first, then patch the true path. */

  /* Restructure: emit loop first, then a forward jump for the exit */
  /* Actually let's do it cleanly:
       condition is true  → pop, OP_JUMP to after OP_LOOP  (exit)
       condition is false → OP_LOOP back to loopStart
     So:
       OP_JUMP_IF_FALSE → loop  (false = keep looping)
       OP_POP (truthy path)
       OP_JUMP → end           (exit)
       OP_POP (falsey path)    ← loop lands here then falls to OP_LOOP
       OP_LOOP loopStart
  */
  /* The above is getting messy. Simpler: negate. "loop while !cond" */
  /* We already emitted:
       expression()
       exitJump = emitJump(OP_JUMP_IF_FALSE)   <-- if false go somewhere
       emitByte(OP_POP)  <-- truthy path
     If truthy: we want to EXIT. So emit OP_JUMP over the loop instruction. */
  int trueExitJump=emitJump(OP_JUMP);  /* truthy = exit */

  /* Falsey path: patch exitJump to here, pop condition, then loop */
  patchJump(exitJump);
  emitByte(OP_POP);
  emitLoop(loopStart);

  /* Truthy exit lands here */
  patchJump(trueExitJump);

  current->loopDepth--;
}

/* ================================================================== */
/*  statement dispatcher                                               */
/* ================================================================== */

static void statement(void) {
  if(match(TOKEN_PRINT)){
    printStatement();
  } else if(match(TOKEN_IF)){
    ifStatement();
  } else if(match(TOKEN_WHILE)){
    whileStatement();
  } else if(match(TOKEN_FOR)){
    forStatement();
  } else if(match(TOKEN_SWITCH)){
    switchStatement();
  } else if(match(TOKEN_CONTINUE)){
    continueStatement();
  } else if(match(TOKEN_REPEAT)){
    repeatStatement();
  } else if(match(TOKEN_LEFT_BRACE)){
    beginScope(); block(); endScope();
  } else {
    expressionStatement();
  }
}

static void synchronize(void) {
  parser.panicMode=false;
  while(parser.current.type!=TOKEN_EOF){
    if(parser.previous.type==TOKEN_SEMICOLON) return;
    switch(parser.current.type){
      case TOKEN_CLASS: case TOKEN_FUN: case TOKEN_VAR: case TOKEN_VAL:
      case TOKEN_FOR: case TOKEN_IF: case TOKEN_WHILE: case TOKEN_SWITCH:
      case TOKEN_PRINT: case TOKEN_RETURN: case TOKEN_REPEAT: return;
      default: ;
    }
    advance();
  }
}

/* ================================================================== */
/*  Variable declarations                                              */
/* ================================================================== */

static uint8_t parseVariable(const char* errorMessage, bool isConst) {
  consume(TOKEN_IDENTIFIER,errorMessage);
  declareVariable(isConst);
  if(current->scopeDepth>0) return 0;
  return identifierConstant(&parser.previous);
}
static void defineVariable(uint8_t global) {
  if(current->scopeDepth>0){markInitialized();return;}
  emitBytes(OP_DEFINE_GLOBAL,global);
}
void varDeclaration(bool isConst) {
  uint8_t global=parseVariable(
    isConst?"Expect variable name after 'val'.":"Expect variable name.",
    isConst);
  if(match(TOKEN_EQUAL)){
    expression();
  } else {
    if(isConst) error("'val' variables must be initialized.");
    emitByte(OP_NIL);
  }
  consume(TOKEN_SEMICOLON,"Expect ';' after variable declaration.");
  defineVariable(global);
}
static void declaration(void) {
  if(match(TOKEN_VAR)){
    varDeclaration(false);
  } else if(match(TOKEN_VAL)){
    varDeclaration(true);
  } else {
    statement();
  }
  if(parser.panicMode) synchronize();
}

/* ================================================================== */
/*  compile()                                                          */
/* ================================================================== */

bool compile(const char* source, Chunk* chunk) {
  initScanner(source);
  Compiler compiler;
  initCompiler(&compiler);
  compilingChunk=chunk;
  parser.hadError=false; parser.panicMode=false;
  resetIdCache();
  advance();
  while(!match(TOKEN_EOF)) declaration();
  endCompiler();
  return !parser.hadError;
}
