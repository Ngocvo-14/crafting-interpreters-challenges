#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "compiler.h"
#include "scanner.h"
#include "object.h"
#include "memory.h"
#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

typedef enum { TYPE_FUNCTION, TYPE_INITIALIZER, TYPE_METHOD, TYPE_SCRIPT } FunctionType;
typedef struct { Token name; int depth; bool isCaptured; } Local;
typedef struct { uint8_t index; bool isLocal; } Upvalue;
typedef struct Compiler {
  struct Compiler* enclosing; ObjFunction* function; FunctionType type;
  Local locals[UINT8_COUNT]; int localCount; int scopeDepth;
  Upvalue upvalues[UINT8_COUNT];
} Compiler;
typedef struct ClassCompiler {
  struct ClassCompiler* enclosing; bool hasSuperclass;
} ClassCompiler;
typedef struct { Token current; Token previous; bool hadError; bool panicMode; } Parser;
static Parser parser;
static Compiler* current=NULL;
static ClassCompiler* currentClass=NULL;
static Chunk* currentChunk(){return &current->function->chunk;}

static void errorAt(Token* t,const char* msg){if(parser.panicMode)return;parser.panicMode=true;fprintf(stderr,"[line %d] Error",t->line);if(t->type==TOKEN_EOF)fprintf(stderr," at end");else if(t->type!=TOKEN_ERROR)fprintf(stderr," at '%.*s'",t->length,t->start);fprintf(stderr,": %s\n",msg);parser.hadError=true;}
static void error(const char* msg){errorAt(&parser.previous,msg);}
static void errorAtCurrent(const char* msg){errorAt(&parser.current,msg);}
static void advance(){parser.previous=parser.current;for(;;){parser.current=scanToken();if(parser.current.type!=TOKEN_ERROR)break;errorAtCurrent(parser.current.start);}}
static void consume(TokenType t,const char* m){if(parser.current.type==t){advance();return;}errorAtCurrent(m);}
static bool check(TokenType t){return parser.current.type==t;}
static bool match(TokenType t){if(!check(t))return false;advance();return true;}
static void emitByte(uint8_t b){writeChunk(currentChunk(),b,parser.previous.line);}
static void emitBytes(uint8_t b1,uint8_t b2){emitByte(b1);emitByte(b2);}
static void emitReturn(){if(current->type==TYPE_INITIALIZER){emitBytes(OP_GET_LOCAL,0);}else{emitByte(OP_NIL);}emitByte(OP_RETURN);}
static uint8_t makeConstant(Value v){int c=addConstant(currentChunk(),v);if(c>255){error("Too many constants.");return 0;}return(uint8_t)c;}
static void emitConstant(Value v){emitBytes(OP_CONSTANT,makeConstant(v));}
static int emitJump(uint8_t b){emitByte(b);emitByte(0xff);emitByte(0xff);return currentChunk()->count-2;}
static void patchJump(int offset){int jump=currentChunk()->count-offset-2;if(jump>UINT16_MAX){error("Too much code.");}currentChunk()->code[offset]=(jump>>8)&0xff;currentChunk()->code[offset+1]=jump&0xff;}
static void emitLoop(int loopStart){emitByte(OP_LOOP);int offset=currentChunk()->count-loopStart+2;if(offset>UINT16_MAX)error("Loop body too large.");emitByte((offset>>8)&0xff);emitByte(offset&0xff);}

static void initCompiler(Compiler* c,FunctionType type){
  c->enclosing=current;c->function=NULL;c->type=type;c->localCount=0;c->scopeDepth=0;
  c->function=newFunction();current=c;
  if(type!=TYPE_SCRIPT)current->function->name=copyString(parser.previous.start,parser.previous.length);
  Local* local=&current->locals[current->localCount++];local->depth=0;local->isCaptured=false;
  if(type!=TYPE_FUNCTION){local->name.start="this";local->name.length=4;}
  else{local->name.start="";local->name.length=0;}
}
static ObjFunction* endCompiler(){
  emitReturn();ObjFunction* fn=current->function;
#ifdef DEBUG_PRINT_CODE
  if(!parser.hadError)disassembleChunk(currentChunk(),fn->name?fn->name->chars:"<script>");
#endif
  current=current->enclosing;return fn;
}
static void beginScope(){current->scopeDepth++;}
static void endScope(){
  current->scopeDepth--;
  while(current->localCount>0&&current->locals[current->localCount-1].depth>current->scopeDepth){
    if(current->locals[current->localCount-1].isCaptured)emitByte(OP_CLOSE_UPVALUE);else emitByte(OP_POP);
    current->localCount--;
  }
}

typedef enum{PREC_NONE,PREC_ASSIGNMENT,PREC_OR,PREC_AND,PREC_EQUALITY,PREC_COMPARISON,PREC_TERM,PREC_FACTOR,PREC_UNARY,PREC_CALL,PREC_PRIMARY}Precedence;
typedef void(*ParseFn)(bool);
typedef struct{ParseFn prefix;ParseFn infix;Precedence precedence;}ParseRule;
static ParseRule* getRule(TokenType t);
static void parsePrecedence(Precedence p);
static void expression(void);static void statement(void);static void declaration(void);static void varDeclaration(void);

static bool identifiersEqual(Token* a,Token* b){if(a->length!=b->length)return false;return memcmp(a->start,b->start,a->length)==0;}
static int resolveLocal(Compiler* c,Token* name){for(int i=c->localCount-1;i>=0;i--){Local* local=&c->locals[i];if(identifiersEqual(name,&local->name)){if(local->depth==-1)error("Can't read local variable in its own initializer.");return i;}}return -1;}
static int addUpvalue(Compiler* c,uint8_t index,bool isLocal){int count=c->function->upvalueCount;for(int i=0;i<count;i++){Upvalue* uv=&c->upvalues[i];if(uv->index==index&&uv->isLocal==isLocal)return i;}if(count==UINT8_COUNT){error("Too many closure variables.");return 0;}c->upvalues[count].isLocal=isLocal;c->upvalues[count].index=index;return c->function->upvalueCount++;}
static int resolveUpvalue(Compiler* c,Token* name){if(!c->enclosing)return -1;int local=resolveLocal(c->enclosing,name);if(local!=-1){c->enclosing->locals[local].isCaptured=true;return addUpvalue(c,(uint8_t)local,true);}int upvalue=resolveUpvalue(c->enclosing,name);if(upvalue!=-1)return addUpvalue(c,(uint8_t)upvalue,false);return -1;}
static void addLocal(Token name){if(current->localCount==UINT8_COUNT){error("Too many local variables.");return;}Local* l=&current->locals[current->localCount++];l->name=name;l->depth=-1;l->isCaptured=false;}
static void declareVariable(){if(current->scopeDepth==0)return;Token* name=&parser.previous;for(int i=current->localCount-1;i>=0;i--){Local* l=&current->locals[i];if(l->depth!=-1&&l->depth<current->scopeDepth)break;if(identifiersEqual(name,&l->name)){error("Already a variable with this name in this scope.");}}addLocal(*name);}
static uint8_t identifierConstant(Token* t){return makeConstant(OBJ_VAL(copyString(t->start,t->length)));}
static uint8_t parseVariable(const char* msg){consume(TOKEN_IDENTIFIER,msg);declareVariable();if(current->scopeDepth>0)return 0;return identifierConstant(&parser.previous);}
static void markInitialized(){if(current->scopeDepth==0)return;current->locals[current->localCount-1].depth=current->scopeDepth;}
static void defineVariable(uint8_t global){if(current->scopeDepth>0){markInitialized();return;}emitBytes(OP_DEFINE_GLOBAL,global);}

static Token syntheticToken(const char* text){Token token;token.start=text;token.length=(int)strlen(text);return token;}
static void namedVariable(Token name,bool canAssign);

static void number(bool ca){(void)ca;emitConstant(NUMBER_VAL(strtod(parser.previous.start,NULL)));}
static void string_(bool ca){(void)ca;emitConstant(OBJ_VAL(copyString(parser.previous.start+1,parser.previous.length-2)));}
static void literal(bool ca){(void)ca;switch(parser.previous.type){case TOKEN_FALSE:emitByte(OP_FALSE);break;case TOKEN_NIL:emitByte(OP_NIL);break;case TOKEN_TRUE:emitByte(OP_TRUE);break;default:return;}}
static void grouping(bool ca){(void)ca;expression();consume(TOKEN_RIGHT_PAREN,"Expect ')' after expression.");}
static void unary(bool ca){(void)ca;TokenType op=parser.previous.type;parsePrecedence(PREC_UNARY);switch(op){case TOKEN_BANG:emitByte(OP_NOT);break;case TOKEN_MINUS:emitByte(OP_NEGATE);break;default:return;}}
static void binary(bool ca){(void)ca;TokenType op=parser.previous.type;ParseRule* rule=getRule(op);parsePrecedence((Precedence)(rule->precedence+1));
  switch(op){case TOKEN_BANG_EQUAL:emitBytes(OP_EQUAL,OP_NOT);break;case TOKEN_EQUAL_EQUAL:emitByte(OP_EQUAL);break;case TOKEN_GREATER:emitByte(OP_GREATER);break;case TOKEN_GREATER_EQUAL:emitBytes(OP_LESS,OP_NOT);break;case TOKEN_LESS:emitByte(OP_LESS);break;case TOKEN_LESS_EQUAL:emitBytes(OP_GREATER,OP_NOT);break;case TOKEN_PLUS:emitByte(OP_ADD);break;case TOKEN_MINUS:emitByte(OP_SUBTRACT);break;case TOKEN_STAR:emitByte(OP_MULTIPLY);break;case TOKEN_SLASH:emitByte(OP_DIVIDE);break;default:return;}
}
static void and_(bool ca){(void)ca;int j=emitJump(OP_JUMP_IF_FALSE);emitByte(OP_POP);parsePrecedence(PREC_AND);patchJump(j);}
static void or_(bool ca){(void)ca;int ej=emitJump(OP_JUMP_IF_FALSE);int endj=emitJump(OP_JUMP);patchJump(ej);emitByte(OP_POP);parsePrecedence(PREC_OR);patchJump(endj);}
static uint8_t argumentList(){uint8_t argc=0;if(!check(TOKEN_RIGHT_PAREN)){do{expression();if(argc==255)error("Can't have more than 255 arguments.");argc++;}while(match(TOKEN_COMMA));}consume(TOKEN_RIGHT_PAREN,"Expect ')' after arguments.");return argc;}
static void call(bool ca){(void)ca;uint8_t argc=argumentList();emitBytes(OP_CALL,argc);}

static void dot(bool canAssign){
  consume(TOKEN_IDENTIFIER,"Expect property name after '.'.");
  uint8_t name=identifierConstant(&parser.previous);
  if(canAssign&&match(TOKEN_EQUAL)){expression();emitBytes(OP_SET_PROPERTY,name);}
  else if(match(TOKEN_LEFT_PAREN)){uint8_t argc=argumentList();emitBytes(OP_INVOKE,name);emitByte(argc);}
  else{emitBytes(OP_GET_PROPERTY,name);}
}
static void subscript(bool canAssign){
  expression();consume(TOKEN_RIGHT_BRACKET,"Expect ']'.");
  if(canAssign&&match(TOKEN_EQUAL)){expression();emitByte(OP_SET_FIELD_DYNAMIC);}
  else emitByte(OP_GET_FIELD_DYNAMIC);
}
static void namedVariable(Token name,bool canAssign){
  uint8_t getOp,setOp;int arg=resolveLocal(current,&name);
  if(arg!=-1){getOp=OP_GET_LOCAL;setOp=OP_SET_LOCAL;}
  else if((arg=resolveUpvalue(current,&name))!=-1){getOp=OP_GET_UPVALUE;setOp=OP_SET_UPVALUE;}
  else{arg=identifierConstant(&name);getOp=OP_GET_GLOBAL;setOp=OP_SET_GLOBAL;}
  if(canAssign&&match(TOKEN_EQUAL)){expression();emitBytes(setOp,(uint8_t)arg);}
  else emitBytes(getOp,(uint8_t)arg);
}
static void variable(bool ca){namedVariable(parser.previous,ca);}
static void this_(bool ca){(void)ca;if(!currentClass){error("Can't use 'this' outside class.");return;}variable(false);}

/* ch29: super expression */
static void super_(bool ca){
  (void)ca;
  if(!currentClass){error("Can't use 'super' outside of a class.");return;}
  if(!currentClass->hasSuperclass){error("Can't use 'super' in a class with no superclass.");return;}
  consume(TOKEN_DOT,"Expect '.' after 'super'.");
  consume(TOKEN_IDENTIFIER,"Expect superclass method name.");
  uint8_t name=identifierConstant(&parser.previous);
  namedVariable(syntheticToken("this"),false);
  if(match(TOKEN_LEFT_PAREN)){
    uint8_t argc=argumentList();
    namedVariable(syntheticToken("super"),false);
    emitBytes(OP_SUPER_INVOKE,name);emitByte(argc);
  } else {
    namedVariable(syntheticToken("super"),false);
    emitBytes(OP_GET_SUPER,name);
  }
}

/* ----------------------------------------------------------------
   Challenge 3: inner() — BETA dispatch
   Emits OP_INNER argCount.
   The VM handles finding the next subclass method at runtime.
---------------------------------------------------------------- */
static void inner_(bool ca){
  (void)ca;
  if(!currentClass){error("Can't use 'inner' outside of a class.");return;}
  consume(TOKEN_LEFT_PAREN,"Expect '(' after 'inner'.");
  uint8_t argc=argumentList();
  emitByte(OP_INNER);
  emitByte(argc);
}

static ParseRule rules[]={
  [TOKEN_LEFT_PAREN]    ={grouping, call,      PREC_CALL},
  [TOKEN_RIGHT_PAREN]   ={NULL,NULL,PREC_NONE},
  [TOKEN_LEFT_BRACE]    ={NULL,NULL,PREC_NONE},
  [TOKEN_RIGHT_BRACE]   ={NULL,NULL,PREC_NONE},
  [TOKEN_LEFT_BRACKET]  ={NULL,subscript,PREC_CALL},
  [TOKEN_RIGHT_BRACKET] ={NULL,NULL,PREC_NONE},
  [TOKEN_COMMA]         ={NULL,NULL,PREC_NONE},
  [TOKEN_DOT]           ={NULL,dot,PREC_CALL},
  [TOKEN_MINUS]         ={unary,binary,PREC_TERM},
  [TOKEN_PLUS]          ={NULL,binary,PREC_TERM},
  [TOKEN_SEMICOLON]     ={NULL,NULL,PREC_NONE},
  [TOKEN_SLASH]         ={NULL,binary,PREC_FACTOR},
  [TOKEN_STAR]          ={NULL,binary,PREC_FACTOR},
  [TOKEN_BANG]          ={unary,NULL,PREC_NONE},
  [TOKEN_BANG_EQUAL]    ={NULL,binary,PREC_EQUALITY},
  [TOKEN_EQUAL]         ={NULL,NULL,PREC_NONE},
  [TOKEN_EQUAL_EQUAL]   ={NULL,binary,PREC_EQUALITY},
  [TOKEN_GREATER]       ={NULL,binary,PREC_COMPARISON},
  [TOKEN_GREATER_EQUAL] ={NULL,binary,PREC_COMPARISON},
  [TOKEN_LESS]          ={NULL,binary,PREC_COMPARISON},
  [TOKEN_LESS_EQUAL]    ={NULL,binary,PREC_COMPARISON},
  [TOKEN_IDENTIFIER]    ={variable,NULL,PREC_NONE},
  [TOKEN_STRING]        ={string_,NULL,PREC_NONE},
  [TOKEN_NUMBER]        ={number,NULL,PREC_NONE},
  [TOKEN_AND]           ={NULL,and_,PREC_AND},
  [TOKEN_CLASS]         ={NULL,NULL,PREC_NONE},
  [TOKEN_DELETE]        ={NULL,NULL,PREC_NONE},
  [TOKEN_ELSE]          ={NULL,NULL,PREC_NONE},
  [TOKEN_FALSE]         ={literal,NULL,PREC_NONE},
  [TOKEN_FOR]           ={NULL,NULL,PREC_NONE},
  [TOKEN_FUN]           ={NULL,NULL,PREC_NONE},
  [TOKEN_HAS]           ={NULL,NULL,PREC_NONE},
  [TOKEN_IF]            ={NULL,NULL,PREC_NONE},
  [TOKEN_INNER]         ={inner_,NULL,PREC_NONE},
  [TOKEN_NIL]           ={literal,NULL,PREC_NONE},
  [TOKEN_OR]            ={NULL,or_,PREC_OR},
  [TOKEN_PRINT]         ={NULL,NULL,PREC_NONE},
  [TOKEN_RETURN]        ={NULL,NULL,PREC_NONE},
  [TOKEN_SUPER]         ={super_,NULL,PREC_NONE},
  [TOKEN_THIS]          ={this_,NULL,PREC_NONE},
  [TOKEN_TRUE]          ={literal,NULL,PREC_NONE},
  [TOKEN_VAR]           ={NULL,NULL,PREC_NONE},
  [TOKEN_WHILE]         ={NULL,NULL,PREC_NONE},
  [TOKEN_ERROR]         ={NULL,NULL,PREC_NONE},
  [TOKEN_EOF]           ={NULL,NULL,PREC_NONE},
};
static ParseRule* getRule(TokenType t){return &rules[t];}
static void parsePrecedence(Precedence p){
  advance();ParseFn prefix=getRule(parser.previous.type)->prefix;
  if(!prefix){error("Expect expression.");return;}
  bool canAssign=p<=PREC_ASSIGNMENT;prefix(canAssign);
  while(p<=getRule(parser.current.type)->precedence){advance();getRule(parser.previous.type)->infix(canAssign);}
  if(canAssign&&match(TOKEN_EQUAL))error("Invalid assignment target.");
}
static void expression(void){parsePrecedence(PREC_ASSIGNMENT);}
static void printStatement(){expression();consume(TOKEN_SEMICOLON,"Expect ';' after value.");emitByte(OP_PRINT);}
static void expressionStatement(){expression();consume(TOKEN_SEMICOLON,"Expect ';'.");emitByte(OP_POP);}
static void block(){while(!check(TOKEN_RIGHT_BRACE)&&!check(TOKEN_EOF))declaration();consume(TOKEN_RIGHT_BRACE,"Expect '}'.");}
static void ifStatement(){consume(TOKEN_LEFT_PAREN,"Expect '('.");expression();consume(TOKEN_RIGHT_PAREN,"Expect ')'.");int tj=emitJump(OP_JUMP_IF_FALSE);emitByte(OP_POP);statement();int ej=emitJump(OP_JUMP);patchJump(tj);emitByte(OP_POP);if(match(TOKEN_ELSE))statement();patchJump(ej);}
static void whileStatement(){int ls=currentChunk()->count;consume(TOKEN_LEFT_PAREN,"Expect '('.");expression();consume(TOKEN_RIGHT_PAREN,"Expect ')'.");int exitJ=emitJump(OP_JUMP_IF_FALSE);emitByte(OP_POP);statement();emitLoop(ls);patchJump(exitJ);emitByte(OP_POP);}
static void forStatement(){
  beginScope();consume(TOKEN_LEFT_PAREN,"Expect '('.");
  if(match(TOKEN_SEMICOLON)){}else if(match(TOKEN_VAR))varDeclaration();else expressionStatement();
  int ls=currentChunk()->count;int exitJ=-1;
  if(!match(TOKEN_SEMICOLON)){expression();consume(TOKEN_SEMICOLON,"Expect ';'.");exitJ=emitJump(OP_JUMP_IF_FALSE);emitByte(OP_POP);}
  if(!match(TOKEN_RIGHT_PAREN)){int bj=emitJump(OP_JUMP);int is=currentChunk()->count;expression();emitByte(OP_POP);consume(TOKEN_RIGHT_PAREN,"Expect ')'.");emitLoop(ls);ls=is;patchJump(bj);}
  statement();emitLoop(ls);if(exitJ!=-1){patchJump(exitJ);emitByte(OP_POP);}endScope();
}
static void returnStatement(){
  if(current->type==TYPE_SCRIPT){error("Can't return from top-level code.");return;}
  if(match(TOKEN_SEMICOLON)){emitReturn();}
  else{if(current->type==TYPE_INITIALIZER)error("Can't return a value from an initializer.");expression();consume(TOKEN_SEMICOLON,"Expect ';'.");emitByte(OP_RETURN);}
}
static void function(FunctionType type){
  Compiler compiler;initCompiler(&compiler,type);beginScope();
  consume(TOKEN_LEFT_PAREN,"Expect '('.");
  if(!check(TOKEN_RIGHT_PAREN)){do{current->function->arity++;if(current->function->arity>255)errorAtCurrent("Can't have more than 255 parameters.");uint8_t c=parseVariable("Expect parameter name.");defineVariable(c);}while(match(TOKEN_COMMA));}
  consume(TOKEN_RIGHT_PAREN,"Expect ')'.");consume(TOKEN_LEFT_BRACE,"Expect '{'.");block();
  ObjFunction* fn=endCompiler();emitBytes(OP_CLOSURE,makeConstant(OBJ_VAL(fn)));
  for(int i=0;i<fn->upvalueCount;i++){emitByte(compiler.upvalues[i].isLocal?1:0);emitByte(compiler.upvalues[i].index);}
}
static void method(){
  consume(TOKEN_IDENTIFIER,"Expect method name.");
  uint8_t constant=identifierConstant(&parser.previous);
  FunctionType type=TYPE_METHOD;
  if(parser.previous.length==4&&memcmp(parser.previous.start,"init",4)==0)type=TYPE_INITIALIZER;
  function(type);emitBytes(OP_METHOD,constant);
}
static void classDeclaration(){
  consume(TOKEN_IDENTIFIER,"Expect class name.");
  Token className=parser.previous;
  uint8_t nameConstant=identifierConstant(&parser.previous);
  declareVariable();emitBytes(OP_CLASS,nameConstant);defineVariable(nameConstant);
  ClassCompiler classCompiler;classCompiler.hasSuperclass=false;classCompiler.enclosing=currentClass;currentClass=&classCompiler;
  if(match(TOKEN_LESS)){
    consume(TOKEN_IDENTIFIER,"Expect superclass name.");
    variable(false);
    if(identifiersEqual(&className,&parser.previous))error("A class can't inherit from itself.");
    beginScope();addLocal(syntheticToken("super"));defineVariable(0);
    namedVariable(className,false);emitByte(OP_INHERIT);classCompiler.hasSuperclass=true;
  }
  namedVariable(className,false);
  consume(TOKEN_LEFT_BRACE,"Expect '{'.");
  while(!check(TOKEN_RIGHT_BRACE)&&!check(TOKEN_EOF))method();
  consume(TOKEN_RIGHT_BRACE,"Expect '}'.");
  emitByte(OP_POP);
  if(classCompiler.hasSuperclass)endScope();
  currentClass=currentClass->enclosing;
}
static void deleteStatement(){consume(TOKEN_IDENTIFIER,"Expect variable.");namedVariable(parser.previous,false);consume(TOKEN_DOT,"Expect '.'.");consume(TOKEN_IDENTIFIER,"Expect field name.");uint8_t name=identifierConstant(&parser.previous);consume(TOKEN_SEMICOLON,"Expect ';'.");emitBytes(OP_DELETE_FIELD,name);}
static void funDeclaration(){uint8_t global=parseVariable("Expect function name.");markInitialized();function(TYPE_FUNCTION);defineVariable(global);}
static void varDeclaration(){uint8_t global=parseVariable("Expect variable name.");if(match(TOKEN_EQUAL))expression();else emitByte(OP_NIL);consume(TOKEN_SEMICOLON,"Expect ';'.");defineVariable(global);}
static void statement(){
  if(match(TOKEN_PRINT))printStatement();
  else if(match(TOKEN_DELETE))deleteStatement();
  else if(match(TOKEN_FOR))forStatement();
  else if(match(TOKEN_IF))ifStatement();
  else if(match(TOKEN_RETURN))returnStatement();
  else if(match(TOKEN_WHILE))whileStatement();
  else if(match(TOKEN_LEFT_BRACE)){beginScope();block();endScope();}
  else expressionStatement();
}
static void synchronize(){parser.panicMode=false;while(parser.current.type!=TOKEN_EOF){if(parser.previous.type==TOKEN_SEMICOLON)return;switch(parser.current.type){case TOKEN_CLASS:case TOKEN_FUN:case TOKEN_VAR:case TOKEN_FOR:case TOKEN_IF:case TOKEN_WHILE:case TOKEN_PRINT:case TOKEN_RETURN:case TOKEN_DELETE:return;default:;}advance();}}
static void declaration(){
  if(match(TOKEN_CLASS))classDeclaration();
  else if(match(TOKEN_FUN))funDeclaration();
  else if(match(TOKEN_VAR))varDeclaration();
  else statement();
  if(parser.panicMode)synchronize();
}
ObjFunction* compile(const char* source){
  initScanner(source);Compiler compiler;initCompiler(&compiler,TYPE_SCRIPT);
  parser.hadError=false;parser.panicMode=false;
  advance();while(!match(TOKEN_EOF))declaration();
  ObjFunction* fn=endCompiler();return parser.hadError?NULL:fn;
}
void markCompilerRoots(void){Compiler* c=current;while(c!=NULL){markObject((Obj*)c->function);c=c->enclosing;}}
