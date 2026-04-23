#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "compiler.h"
#include "scanner.h"
#include "object.h"
#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

typedef enum { TYPE_FUNCTION, TYPE_SCRIPT } FunctionType;

typedef struct { Token name; int depth; } Local;

typedef struct Compiler {
  struct Compiler* enclosing;
  ObjFunction*     function;
  FunctionType     type;
  Local   locals[UINT8_COUNT];
  int     localCount;
  int     scopeDepth;
} Compiler;

typedef struct { Token current; Token previous; bool hadError; bool panicMode; } Parser;
static Parser    parser;
static Compiler* current = NULL;
static Chunk*    currentChunk() { return &current->function->chunk; }

/* ---- errors ---- */
static void errorAt(Token* t, const char* msg) {
  if(parser.panicMode) return;
  parser.panicMode=true;
  fprintf(stderr,"[line %d] Error",t->line);
  if(t->type==TOKEN_EOF)         fprintf(stderr," at end");
  else if(t->type!=TOKEN_ERROR)  fprintf(stderr," at '%.*s'",t->length,t->start);
  fprintf(stderr,": %s\n",msg);
  parser.hadError=true;
}
static void error(const char* msg)         { errorAt(&parser.previous,msg); }
static void errorAtCurrent(const char* msg){ errorAt(&parser.current,msg); }

/* ---- tokens ---- */
static void advance(){ parser.previous=parser.current; for(;;){parser.current=scanToken();if(parser.current.type!=TOKEN_ERROR)break;errorAtCurrent(parser.current.start);} }
static void consume(TokenType t,const char* m){ if(parser.current.type==t){advance();return;} errorAtCurrent(m); }
static bool check(TokenType t){ return parser.current.type==t; }
static bool match(TokenType t){ if(!check(t))return false; advance();return true; }

/* ---- emit ---- */
static void emitByte(uint8_t b){ writeChunk(currentChunk(),b,parser.previous.line); }
static void emitBytes(uint8_t b1,uint8_t b2){ emitByte(b1);emitByte(b2); }
static void emitReturn(){ emitByte(OP_NIL); emitByte(OP_RETURN); }
static uint8_t makeConstant(Value v){ int c=addConstant(currentChunk(),v); if(c>255){error("Too many constants.");return 0;} return(uint8_t)c; }
static void emitConstant(Value v){ emitBytes(OP_CONSTANT,makeConstant(v)); }
static int  emitJump(uint8_t b){ emitByte(b);emitByte(0xff);emitByte(0xff);return currentChunk()->count-2; }
static void patchJump(int offset){ int jump=currentChunk()->count-offset-2; if(jump>UINT16_MAX){error("Too much code to jump over.");} currentChunk()->code[offset]=(jump>>8)&0xff; currentChunk()->code[offset+1]=jump&0xff; }
static void emitLoop(int loopStart){ emitByte(OP_LOOP);int offset=currentChunk()->count-loopStart+2;if(offset>UINT16_MAX)error("Loop body too large.");emitByte((offset>>8)&0xff);emitByte(offset&0xff); }

/* ---- compiler lifecycle ---- */
static void initCompiler(Compiler* c, FunctionType type){
  c->enclosing=current; c->function=NULL; c->type=type;
  c->localCount=0; c->scopeDepth=0;
  c->function=newFunction(); current=c;
  if(type!=TYPE_SCRIPT) current->function->name=copyString(parser.previous.start,parser.previous.length);
  Local* local=&current->locals[current->localCount++];
  local->depth=0; local->name.start=""; local->name.length=0;
}
static ObjFunction* endCompiler(){
  emitReturn();
  ObjFunction* fn=current->function;
#ifdef DEBUG_PRINT_CODE
  if(!parser.hadError) disassembleChunk(currentChunk(),fn->name?fn->name->chars:"<script>");
#endif
  current=current->enclosing;
  return fn;
}

/* ---- scope ---- */
static void beginScope(){ current->scopeDepth++; }
static void endScope(){
  current->scopeDepth--;
  while(current->localCount>0&&current->locals[current->localCount-1].depth>current->scopeDepth){
    emitByte(OP_POP); current->localCount--;
  }
}

/* ---- forward decls ---- */
typedef enum { PREC_NONE,PREC_ASSIGNMENT,PREC_OR,PREC_AND,PREC_EQUALITY,PREC_COMPARISON,PREC_TERM,PREC_FACTOR,PREC_UNARY,PREC_CALL,PREC_PRIMARY } Precedence;
typedef void(*ParseFn)(bool);
typedef struct { ParseFn prefix; ParseFn infix; Precedence precedence; } ParseRule;
static ParseRule* getRule(TokenType t);
static void parsePrecedence(Precedence p);
static void expression(void);
static void statement(void);
static void declaration(void);
static void varDeclaration(void);
static uint8_t identifierConstant(Token* t);
static int  resolveLocal(Compiler* c,Token* t);
static void defineVariable(uint8_t global);
static void markInitialized(void);

/* ---- expression parsers ---- */
static void number(bool ca){ (void)ca; emitConstant(NUMBER_VAL(strtod(parser.previous.start,NULL))); }
static void string_(bool ca){ (void)ca; emitConstant(OBJ_VAL(copyString(parser.previous.start+1,parser.previous.length-2))); }
static void literal(bool ca){ (void)ca; switch(parser.previous.type){case TOKEN_FALSE:emitByte(OP_FALSE);break;case TOKEN_NIL:emitByte(OP_NIL);break;case TOKEN_TRUE:emitByte(OP_TRUE);break;default:return;} }
static void grouping(bool ca){ (void)ca; expression(); consume(TOKEN_RIGHT_PAREN,"Expect ')' after expression."); }
static void unary(bool ca){ (void)ca; TokenType op=parser.previous.type; parsePrecedence(PREC_UNARY); switch(op){case TOKEN_BANG:emitByte(OP_NOT);break;case TOKEN_MINUS:emitByte(OP_NEGATE);break;default:return;} }
static void binary(bool ca){ (void)ca; TokenType op=parser.previous.type; ParseRule* rule=getRule(op); parsePrecedence((Precedence)(rule->precedence+1));
  switch(op){case TOKEN_BANG_EQUAL:emitBytes(OP_EQUAL,OP_NOT);break;case TOKEN_EQUAL_EQUAL:emitByte(OP_EQUAL);break;case TOKEN_GREATER:emitByte(OP_GREATER);break;case TOKEN_GREATER_EQUAL:emitBytes(OP_LESS,OP_NOT);break;case TOKEN_LESS:emitByte(OP_LESS);break;case TOKEN_LESS_EQUAL:emitBytes(OP_GREATER,OP_NOT);break;case TOKEN_PLUS:emitByte(OP_ADD);break;case TOKEN_MINUS:emitByte(OP_SUBTRACT);break;case TOKEN_STAR:emitByte(OP_MULTIPLY);break;case TOKEN_SLASH:emitByte(OP_DIVIDE);break;default:return;}
}
static void and_(bool ca){ (void)ca; int j=emitJump(OP_JUMP_IF_FALSE);emitByte(OP_POP);parsePrecedence(PREC_AND);patchJump(j); }
static void or_(bool ca){ (void)ca; int ej=emitJump(OP_JUMP_IF_FALSE);int endj=emitJump(OP_JUMP);patchJump(ej);emitByte(OP_POP);parsePrecedence(PREC_OR);patchJump(endj); }

static uint8_t argumentList(){
  uint8_t argc=0;
  if(!check(TOKEN_RIGHT_PAREN)){
    do{ expression(); if(argc==255)error("Can't have more than 255 arguments."); argc++; }while(match(TOKEN_COMMA));
  }
  consume(TOKEN_RIGHT_PAREN,"Expect ')' after arguments."); return argc;
}
static void call(bool ca){ (void)ca; uint8_t argc=argumentList(); emitBytes(OP_CALL,argc); }

static void namedVariable(Token name, bool canAssign){
  uint8_t getOp, setOp;
  int arg=resolveLocal(current,&name);
  if(arg!=-1){ getOp=OP_GET_LOCAL; setOp=OP_SET_LOCAL; }
  else       { arg=identifierConstant(&name); getOp=OP_GET_GLOBAL; setOp=OP_SET_GLOBAL; }
  if(canAssign&&match(TOKEN_EQUAL)){ expression(); emitBytes(setOp,(uint8_t)arg); }
  else { emitBytes(getOp,(uint8_t)arg); }
}
static void variable(bool ca){ namedVariable(parser.previous,ca); }

static ParseRule rules[]={
  [TOKEN_LEFT_PAREN]   ={grouping,call,  PREC_CALL},
  [TOKEN_RIGHT_PAREN]  ={NULL,    NULL,  PREC_NONE},
  [TOKEN_LEFT_BRACE]   ={NULL,    NULL,  PREC_NONE},
  [TOKEN_RIGHT_BRACE]  ={NULL,    NULL,  PREC_NONE},
  [TOKEN_COMMA]        ={NULL,    NULL,  PREC_NONE},
  [TOKEN_DOT]          ={NULL,    NULL,  PREC_NONE},
  [TOKEN_MINUS]        ={unary,   binary,PREC_TERM},
  [TOKEN_PLUS]         ={NULL,    binary,PREC_TERM},
  [TOKEN_SEMICOLON]    ={NULL,    NULL,  PREC_NONE},
  [TOKEN_SLASH]        ={NULL,    binary,PREC_FACTOR},
  [TOKEN_STAR]         ={NULL,    binary,PREC_FACTOR},
  [TOKEN_BANG]         ={unary,   NULL,  PREC_NONE},
  [TOKEN_BANG_EQUAL]   ={NULL,    binary,PREC_EQUALITY},
  [TOKEN_EQUAL]        ={NULL,    NULL,  PREC_NONE},
  [TOKEN_EQUAL_EQUAL]  ={NULL,    binary,PREC_EQUALITY},
  [TOKEN_GREATER]      ={NULL,    binary,PREC_COMPARISON},
  [TOKEN_GREATER_EQUAL]={NULL,    binary,PREC_COMPARISON},
  [TOKEN_LESS]         ={NULL,    binary,PREC_COMPARISON},
  [TOKEN_LESS_EQUAL]   ={NULL,    binary,PREC_COMPARISON},
  [TOKEN_IDENTIFIER]   ={variable,NULL,  PREC_NONE},
  [TOKEN_STRING]       ={string_, NULL,  PREC_NONE},
  [TOKEN_NUMBER]       ={number,  NULL,  PREC_NONE},
  [TOKEN_AND]          ={NULL,    and_,  PREC_AND},
  [TOKEN_CLASS]        ={NULL,    NULL,  PREC_NONE},
  [TOKEN_ELSE]         ={NULL,    NULL,  PREC_NONE},
  [TOKEN_FALSE]        ={literal, NULL,  PREC_NONE},
  [TOKEN_FOR]          ={NULL,    NULL,  PREC_NONE},
  [TOKEN_FUN]          ={NULL,    NULL,  PREC_NONE},
  [TOKEN_IF]           ={NULL,    NULL,  PREC_NONE},
  [TOKEN_NIL]          ={literal, NULL,  PREC_NONE},
  [TOKEN_OR]           ={NULL,    or_,   PREC_OR},
  [TOKEN_PRINT]        ={NULL,    NULL,  PREC_NONE},
  [TOKEN_RETURN]       ={NULL,    NULL,  PREC_NONE},
  [TOKEN_SUPER]        ={NULL,    NULL,  PREC_NONE},
  [TOKEN_THIS]         ={NULL,    NULL,  PREC_NONE},
  [TOKEN_TRUE]         ={literal, NULL,  PREC_NONE},
  [TOKEN_VAR]          ={NULL,    NULL,  PREC_NONE},
  [TOKEN_WHILE]        ={NULL,    NULL,  PREC_NONE},
  [TOKEN_ERROR]        ={NULL,    NULL,  PREC_NONE},
  [TOKEN_EOF]          ={NULL,    NULL,  PREC_NONE},
};
static ParseRule* getRule(TokenType t){ return &rules[t]; }
static void parsePrecedence(Precedence p){
  advance(); ParseFn prefix=getRule(parser.previous.type)->prefix;
  if(!prefix){error("Expect expression.");return;}
  bool canAssign=p<=PREC_ASSIGNMENT; prefix(canAssign);
  while(p<=getRule(parser.current.type)->precedence){advance();getRule(parser.previous.type)->infix(canAssign);}
  if(canAssign&&match(TOKEN_EQUAL)) error("Invalid assignment target.");
}
static void expression(void){ parsePrecedence(PREC_ASSIGNMENT); }

/* ---- variables ---- */
static bool identifiersEqual(Token* a,Token* b){ if(a->length!=b->length)return false;return memcmp(a->start,b->start,a->length)==0; }
static int resolveLocal(Compiler* c,Token* name){
  for(int i=c->localCount-1;i>=0;i--){
    Local* local=&c->locals[i];
    if(identifiersEqual(name,&local->name)){
      if(local->depth==-1)error("Can't read local variable in its own initializer.");
      return i;
    }
  }
  return -1;
}
static void addLocal(Token name){ if(current->localCount==UINT8_COUNT){error("Too many local variables.");return;} Local* l=&current->locals[current->localCount++];l->name=name;l->depth=-1; }
static void declareVariable(){ if(current->scopeDepth==0)return; Token* name=&parser.previous; for(int i=current->localCount-1;i>=0;i--){Local* l=&current->locals[i];if(l->depth!=-1&&l->depth<current->scopeDepth)break;if(identifiersEqual(name,&l->name)){error("Already a variable with this name in this scope.");}} addLocal(*name); }
static uint8_t identifierConstant(Token* t){ return makeConstant(OBJ_VAL(copyString(t->start,t->length))); }
static uint8_t parseVariable(const char* msg){ consume(TOKEN_IDENTIFIER,msg); declareVariable(); if(current->scopeDepth>0)return 0; return identifierConstant(&parser.previous); }
static void markInitialized(){ if(current->scopeDepth==0)return; current->locals[current->localCount-1].depth=current->scopeDepth; }
static void defineVariable(uint8_t global){ if(current->scopeDepth>0){markInitialized();return;} emitBytes(OP_DEFINE_GLOBAL,global); }

/* ---- statements ---- */
static void printStatement(){ expression();consume(TOKEN_SEMICOLON,"Expect ';' after value.");emitByte(OP_PRINT); }
static void expressionStatement(){ expression();consume(TOKEN_SEMICOLON,"Expect ';' after expression.");emitByte(OP_POP); }
static void block(){ while(!check(TOKEN_RIGHT_BRACE)&&!check(TOKEN_EOF))declaration(); consume(TOKEN_RIGHT_BRACE,"Expect '}' after block."); }

static void ifStatement(){
  consume(TOKEN_LEFT_PAREN,"Expect '(' after 'if'."); expression(); consume(TOKEN_RIGHT_PAREN,"Expect ')' after condition.");
  int thenJump=emitJump(OP_JUMP_IF_FALSE); emitByte(OP_POP); statement();
  int elseJump=emitJump(OP_JUMP); patchJump(thenJump); emitByte(OP_POP);
  if(match(TOKEN_ELSE)) statement();
  patchJump(elseJump);
}
static void whileStatement(){
  int loopStart=currentChunk()->count;
  consume(TOKEN_LEFT_PAREN,"Expect '(' after 'while'."); expression(); consume(TOKEN_RIGHT_PAREN,"Expect ')' after condition.");
  int exitJump=emitJump(OP_JUMP_IF_FALSE); emitByte(OP_POP); statement();
  emitLoop(loopStart); patchJump(exitJump); emitByte(OP_POP);
}
static void forStatement(){
  beginScope();
  consume(TOKEN_LEFT_PAREN,"Expect '(' after 'for'.");
  if(match(TOKEN_SEMICOLON)){}
  else if(match(TOKEN_VAR)) varDeclaration();
  else expressionStatement();
  int loopStart=currentChunk()->count; int exitJump=-1;
  if(!match(TOKEN_SEMICOLON)){ expression(); consume(TOKEN_SEMICOLON,"Expect ';' after loop condition."); exitJump=emitJump(OP_JUMP_IF_FALSE); emitByte(OP_POP); }
  if(!match(TOKEN_RIGHT_PAREN)){ int bodyJump=emitJump(OP_JUMP); int incStart=currentChunk()->count; expression(); emitByte(OP_POP); consume(TOKEN_RIGHT_PAREN,"Expect ')' after for clauses."); emitLoop(loopStart); loopStart=incStart; patchJump(bodyJump); }
  statement(); emitLoop(loopStart);
  if(exitJump!=-1){ patchJump(exitJump); emitByte(OP_POP); }
  endScope();
}
static void returnStatement(){
  if(current->type==TYPE_SCRIPT){error("Can't return from top-level code.");return;}
  if(match(TOKEN_SEMICOLON)){ emitReturn(); }
  else{ expression(); consume(TOKEN_SEMICOLON,"Expect ';' after return value."); emitByte(OP_RETURN); }
}
static void function(FunctionType type){
  Compiler compiler; initCompiler(&compiler,type); beginScope();
  consume(TOKEN_LEFT_PAREN,"Expect '(' after function name.");
  if(!check(TOKEN_RIGHT_PAREN)){
    do{
      current->function->arity++;
      if(current->function->arity>255) errorAtCurrent("Can't have more than 255 parameters.");
      uint8_t constant=parseVariable("Expect parameter name.");
      defineVariable(constant);
    }while(match(TOKEN_COMMA));
  }
  consume(TOKEN_RIGHT_PAREN,"Expect ')' after parameters.");
  consume(TOKEN_LEFT_BRACE,"Expect '{' before function body."); block();
  ObjFunction* fn=endCompiler();
  emitBytes(OP_CONSTANT,makeConstant(OBJ_VAL(fn)));
}
static void funDeclaration(){ uint8_t global=parseVariable("Expect function name."); markInitialized(); function(TYPE_FUNCTION); defineVariable(global); }
static void varDeclaration(){ uint8_t global=parseVariable("Expect variable name."); if(match(TOKEN_EQUAL))expression();else emitByte(OP_NIL); consume(TOKEN_SEMICOLON,"Expect ';' after variable declaration."); defineVariable(global); }

static void statement(){
  if(match(TOKEN_PRINT))        printStatement();
  else if(match(TOKEN_FOR))     forStatement();
  else if(match(TOKEN_IF))      ifStatement();
  else if(match(TOKEN_RETURN))  returnStatement();
  else if(match(TOKEN_WHILE))   whileStatement();
  else if(match(TOKEN_LEFT_BRACE)){ beginScope(); block(); endScope(); }
  else expressionStatement();
}
static void synchronize(){ parser.panicMode=false; while(parser.current.type!=TOKEN_EOF){if(parser.previous.type==TOKEN_SEMICOLON)return;switch(parser.current.type){case TOKEN_CLASS:case TOKEN_FUN:case TOKEN_VAR:case TOKEN_FOR:case TOKEN_IF:case TOKEN_WHILE:case TOKEN_PRINT:case TOKEN_RETURN:return;default:;}advance();} }
static void declaration(){ if(match(TOKEN_FUN))funDeclaration();else if(match(TOKEN_VAR))varDeclaration();else statement(); if(parser.panicMode)synchronize(); }

ObjFunction* compile(const char* source){
  initScanner(source);
  Compiler compiler; initCompiler(&compiler,TYPE_SCRIPT);
  parser.hadError=false; parser.panicMode=false;
  advance();
  while(!match(TOKEN_EOF)) declaration();
  ObjFunction* fn=endCompiler();
  return parser.hadError?NULL:fn;
}
