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

/* ------------------------------------------------------------------ */
/*  Parser state                                                        */
/* ------------------------------------------------------------------ */

typedef struct {
	  Token current;
	    Token previous;
	      bool  hadError;
	        bool  panicMode;
} Parser;

static Parser parser;
static Chunk* compilingChunk;
static Chunk* currentChunk() { return compilingChunk; }

/* ------------------------------------------------------------------ */
/*  Error handling                                                      */
/* ------------------------------------------------------------------ */

static void errorAt(Token* token, const char* msg) {
	  if (parser.panicMode) return;
	    parser.panicMode = true;
	      fprintf(stderr, "[line %d] Error", token->line);
	        if      (token->type == TOKEN_EOF)   fprintf(stderr, " at end");
		  else if (token->type != TOKEN_ERROR) fprintf(stderr, " at '%.*s'",
				                                                 token->length, token->start);
		    fprintf(stderr, ": %s\n", msg);
		      parser.hadError = true;
}
static void error(const char* msg)          { errorAt(&parser.previous, msg); }
static void errorAtCurrent(const char* msg) { errorAt(&parser.current,  msg); }

/* ------------------------------------------------------------------ */
/*  Token helpers                                                       */
/* ------------------------------------------------------------------ */

static void advance() {
	  parser.previous = parser.current;
	    for (;;) {
		        parser.current = scanToken();
			    if (parser.current.type != TOKEN_ERROR) break;
			        errorAtCurrent(parser.current.start);
				  }
}

static void consume(TokenType type, const char* msg) {
	  if (parser.current.type == type) { advance(); return; }
	    errorAtCurrent(msg);
}

static bool check(TokenType type) { return parser.current.type == type; }

static bool match(TokenType type) {
	  if (!check(type)) return false;
	    advance();
	      return true;
}

/* ------------------------------------------------------------------ */
/*  Emit helpers                                                        */
/* ------------------------------------------------------------------ */

static void emitByte(uint8_t byte) {
	  writeChunk(currentChunk(), byte, parser.previous.line);
}
static void emitBytes(uint8_t b1, uint8_t b2) { emitByte(b1); emitByte(b2); }
static void emitReturn()                        { emitByte(OP_RETURN); }

static uint8_t makeConstant(Value value) {
	  int c = addConstant(currentChunk(), value);
	    if (c > 255) { error("Too many constants."); return 0; }
	      return (uint8_t)c;
}

static void emitConstant(Value value) {
	  emitBytes(OP_CONSTANT, makeConstant(value));
}

static void endCompiler() {
	  emitReturn();
#ifdef DEBUG_PRINT_CODE
	    if (!parser.hadError) disassembleChunk(currentChunk(), "code");
#endif
}

/* ------------------------------------------------------------------ */
/*  Challenge 1: Deduplicated identifier constants                     */
/*                                                                      */
/*  The original identifierConstant() always calls makeConstant()      */
/*  which appends a new entry to the constant table every time the     */
/*  same variable name is seen.  With 256 constant slots per chunk,    */
/*  a function that reads the same global 50 times wastes 49 slots.   */
/*                                                                      */
/*  Fix: keep a small hash table (idCache) that maps identifier        */
/*  strings → their existing constant-table index.  Before adding a   */
/*  new constant, check the cache.  If found, reuse the index.         */
/*                                                                      */
/*  Effect on compiler performance: one extra hash lookup per          */
/*  identifier reference — negligible, O(1) average.                  */
/*                                                                      */
/*  Effect on runtime performance: unchanged for the first reference;  */
/*  all subsequent references to the same name reuse the same          */
/*  constant slot → fewer cache misses when the VM reads the constant  */
/*  table, and we conserve constant slots for other constants.         */
/*                                                                      */
/*  Trade-off: the cache uses a small amount of extra memory and       */
/*  adds a tiny amount of compiler complexity.  Given that the         */
/*  alternative is silently hitting the 256-constant limit in real     */
/*  programs, this is clearly worth it.                                */
/* ------------------------------------------------------------------ */

/* We use a simple open-addressing table: key=ObjString*, val=uint8_t index */
#define ID_CACHE_SIZE 64   /* power of two for fast modulo */

typedef struct {
	  ObjString* key;
	    uint8_t    index;
	      bool       occupied;
} IdCacheEntry;

static IdCacheEntry idCache[ID_CACHE_SIZE];
static int          idCacheCount = 0;

static void resetIdCache() {
	  memset(idCache, 0, sizeof(idCache));
	    idCacheCount = 0;
}

/* Look up or insert; returns the constant-table index for this name */
static uint8_t identifierConstant(Token* name) {
	  /* Intern the string first (gives us a canonical pointer) */
	  ObjString* str = copyString(name->start, name->length);

	    /* Search the cache */
	    uint32_t hash  = str->hash;
	      uint32_t idx   = hash & (ID_CACHE_SIZE - 1);
	        for (int i = 0; i < ID_CACHE_SIZE; i++) {
			    uint32_t slot = (idx + i) & (ID_CACHE_SIZE - 1);
			        IdCacheEntry* e = &idCache[slot];
				    if (!e->occupied)    break;          /* not in cache */
				        if (e->key == str)   return e->index; /* cache hit — reuse! */
					  }

		  /* Cache miss: add to constant table then cache the result */
		  uint8_t constIdx = makeConstant(OBJ_VAL(str));

		    /* Insert into cache (no resize — we just let old entries get
		     *    * evicted if the cache is full; correctness is not affected) */
		    if (idCacheCount < ID_CACHE_SIZE) {
			        uint32_t slot = hash & (ID_CACHE_SIZE - 1);
				    for (int i = 0; i < ID_CACHE_SIZE; i++) {
					          uint32_t s = (slot + i) & (ID_CACHE_SIZE - 1);
						        if (!idCache[s].occupied) {
								        idCache[s].key      = str;
									        idCache[s].index    = constIdx;
										        idCache[s].occupied = true;
											        idCacheCount++;
												        break;
													      }
							    }
				      }

		      return constIdx;
}

/* ------------------------------------------------------------------ */
/*  Forward declarations                                               */
/* ------------------------------------------------------------------ */

typedef enum {
	  PREC_NONE, PREC_ASSIGNMENT, PREC_OR, PREC_AND, PREC_EQUALITY,
	    PREC_COMPARISON, PREC_TERM, PREC_FACTOR, PREC_UNARY, PREC_CALL, PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)(bool canAssign);
typedef struct { ParseFn prefix; ParseFn infix; Precedence precedence; } ParseRule;

static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);
static void expression(void);
static void statement(void);
static void declaration(void);

/* ------------------------------------------------------------------ */
/*  Parse functions                                                    */
/* ------------------------------------------------------------------ */

static void number(bool canAssign) {
	  (void)canAssign;
	    double value = strtod(parser.previous.start, NULL);
	      emitConstant(NUMBER_VAL(value));
}

static void string(bool canAssign) {
	  (void)canAssign;
	    emitConstant(OBJ_VAL(constString(parser.previous.start + 1,
					                                        parser.previous.length - 2)));
}

static void literal(bool canAssign) {
	  (void)canAssign;
	    switch (parser.previous.type) {
		        case TOKEN_FALSE: emitByte(OP_FALSE); break;
					      case TOKEN_NIL:   emitByte(OP_NIL);   break;
								    case TOKEN_TRUE:  emitByte(OP_TRUE);  break;
										          default: return;
												     }
}

static void grouping(bool canAssign) {
	  (void)canAssign;
	    expression();
	      consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void unary(bool canAssign) {
	  (void)canAssign;
	    TokenType op = parser.previous.type;
	      parsePrecedence(PREC_UNARY);
	        switch (op) {
			    case TOKEN_BANG:  emitByte(OP_NOT);    break;
					          case TOKEN_MINUS: emitByte(OP_NEGATE); break;
								        default: return;
										   }
}

static void binary(bool canAssign) {
	  (void)canAssign;
	    TokenType op   = parser.previous.type;
	      ParseRule* rule = getRule(op);
	        parsePrecedence((Precedence)(rule->precedence + 1));
		  switch (op) {
			      case TOKEN_BANG_EQUAL:    emitBytes(OP_EQUAL, OP_NOT); break;
							    case TOKEN_EQUAL_EQUAL:   emitByte(OP_EQUAL);          break;
										          case TOKEN_GREATER:       emitByte(OP_GREATER);        break;
														        case TOKEN_GREATER_EQUAL: emitBytes(OP_LESS, OP_NOT);  break;
																		      case TOKEN_LESS:          emitByte(OP_LESS);           break;
																						    case TOKEN_LESS_EQUAL:    emitBytes(OP_GREATER, OP_NOT); break;
																									          case TOKEN_PLUS:          emitByte(OP_ADD);            break;
																													        case TOKEN_MINUS:         emitByte(OP_SUBTRACT);       break;
																																	      case TOKEN_STAR:          emitByte(OP_MULTIPLY);       break;
																																					    case TOKEN_SLASH:         emitByte(OP_DIVIDE);         break;
																																								          default: return;
																																										     }
}

/* namedVariable: handles both GET and SET for global variables */
static void namedVariable(Token name, bool canAssign) {
	  uint8_t arg = identifierConstant(&name);  /* deduplicated! */
	    if (canAssign && match(TOKEN_EQUAL)) {
		        expression();
			    emitBytes(OP_SET_GLOBAL, arg);
			      } else {
				          emitBytes(OP_GET_GLOBAL, arg);
					    }
}

static void variable(bool canAssign) {
	  namedVariable(parser.previous, canAssign);
}

/* ------------------------------------------------------------------ */
/*  Parse table                                                        */
/* ------------------------------------------------------------------ */

static ParseRule rules[] = {
	  [TOKEN_LEFT_PAREN]    = {grouping, NULL,   PREC_NONE},
	    [TOKEN_RIGHT_PAREN]   = {NULL,     NULL,   PREC_NONE},
	      [TOKEN_LEFT_BRACE]    = {NULL,     NULL,   PREC_NONE},
	        [TOKEN_RIGHT_BRACE]   = {NULL,     NULL,   PREC_NONE},
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
						      [TOKEN_AND]           = {NULL,     NULL,   PREC_NONE},
						        [TOKEN_CLASS]         = {NULL,     NULL,   PREC_NONE},
							  [TOKEN_ELSE]          = {NULL,     NULL,   PREC_NONE},
							    [TOKEN_FALSE]         = {literal,  NULL,   PREC_NONE},
							      [TOKEN_FOR]           = {NULL,     NULL,   PREC_NONE},
							        [TOKEN_FUN]           = {NULL,     NULL,   PREC_NONE},
								  [TOKEN_IF]            = {NULL,     NULL,   PREC_NONE},
								    [TOKEN_NIL]           = {literal,  NULL,   PREC_NONE},
								      [TOKEN_OR]            = {NULL,     NULL,   PREC_NONE},
								        [TOKEN_PRINT]         = {NULL,     NULL,   PREC_NONE},
									  [TOKEN_RETURN]        = {NULL,     NULL,   PREC_NONE},
									    [TOKEN_SUPER]         = {NULL,     NULL,   PREC_NONE},
									      [TOKEN_THIS]          = {NULL,     NULL,   PREC_NONE},
									        [TOKEN_TRUE]          = {literal,  NULL,   PREC_NONE},
										  [TOKEN_VAR]           = {NULL,     NULL,   PREC_NONE},
										    [TOKEN_WHILE]         = {NULL,     NULL,   PREC_NONE},
										      [TOKEN_ERROR]         = {NULL,     NULL,   PREC_NONE},
										        [TOKEN_EOF]           = {NULL,     NULL,   PREC_NONE},
};

static ParseRule* getRule(TokenType type) { return &rules[type]; }

static void parsePrecedence(Precedence precedence) {
	  advance();
	    ParseFn prefix = getRule(parser.previous.type)->prefix;
	      if (!prefix) { error("Expect expression."); return; }

	        bool canAssign = precedence <= PREC_ASSIGNMENT;
		  prefix(canAssign);

		    while (precedence <= getRule(parser.current.type)->precedence) {
			        advance();
				    ParseFn infix = getRule(parser.previous.type)->infix;
				        infix(canAssign);
					  }

		      if (canAssign && match(TOKEN_EQUAL)) {
			          error("Invalid assignment target.");
				    }
}

static void expression(void) { parsePrecedence(PREC_ASSIGNMENT); }

/* ------------------------------------------------------------------ */
/*  Statements                                                         */
/* ------------------------------------------------------------------ */

static void printStatement(void) {
	  expression();
	    consume(TOKEN_SEMICOLON, "Expect ';' after value.");
	      emitByte(OP_PRINT);
}

static void expressionStatement(void) {
	  expression();
	    consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
	      emitByte(OP_POP);
}

static void synchronize(void) {
	  parser.panicMode = false;
	    while (parser.current.type != TOKEN_EOF) {
		        if (parser.previous.type == TOKEN_SEMICOLON) return;
			    switch (parser.current.type) {
				          case TOKEN_CLASS: case TOKEN_FUN: case TOKEN_VAR:
						        case TOKEN_FOR:   case TOKEN_IF:  case TOKEN_WHILE:
						        case TOKEN_PRINT: case TOKEN_RETURN: return;
											           default: ;
													        }
			        advance();
				  }
}

static void statement(void) {
	  if (match(TOKEN_PRINT)) {
		      printStatement();
		        } else {
				    expressionStatement();
				      }
}

/* ------------------------------------------------------------------ */
/*  Variable declarations                                              */
/* ------------------------------------------------------------------ */

static uint8_t parseVariable(const char* errorMessage) {
	  consume(TOKEN_IDENTIFIER, errorMessage);
	    return identifierConstant(&parser.previous);
}

static void defineVariable(uint8_t global) {
	  emitBytes(OP_DEFINE_GLOBAL, global);
}

static void varDeclaration(void) {
	  uint8_t global = parseVariable("Expect variable name.");
	    if (match(TOKEN_EQUAL)) {
		        expression();
			  } else {
				      emitByte(OP_NIL);
				        }
	      consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");
	        defineVariable(global);
}

static void declaration(void) {
	  if (match(TOKEN_VAR)) {
		      varDeclaration();
		        } else {
				    statement();
				      }
	    if (parser.panicMode) synchronize();
}

/* ------------------------------------------------------------------ */
/*  compile()                                                          */
/* ------------------------------------------------------------------ */

bool compile(const char* source, Chunk* chunk) {
	  initScanner(source);
	    compilingChunk  = chunk;
	      parser.hadError  = false;
	        parser.panicMode = false;

		  resetIdCache();   /* Challenge 1: fresh cache per compilation unit */

		    advance();
		      while (!match(TOKEN_EOF)) {
			          declaration();
				    }
		        endCompiler();
			  return !parser.hadError;
}
