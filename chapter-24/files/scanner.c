#include <stdio.h>
#include <string.h>
#include "common.h"
#include "scanner.h"
typedef struct { const char* start; const char* current; int line; } Scanner;
static Scanner scanner;
void initScanner(const char* source) { scanner.start=source; scanner.current=source; scanner.line=1; }
static bool isAtEnd()       { return *scanner.current=='\0'; }
static bool isDigit(char c) { return c>='0'&&c<='9'; }
static bool isAlpha(char c) { return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'; }
static char advance()       { return *scanner.current++; }
static char peek()          { return *scanner.current; }
static char peekNext()      { return isAtEnd()?'\0':scanner.current[1]; }
static bool match(char e)   { if(isAtEnd()||*scanner.current!=e)return false; scanner.current++;return true; }
static Token makeToken(TokenType t){ Token tok;tok.type=t;tok.start=scanner.start;tok.length=(int)(scanner.current-scanner.start);tok.line=scanner.line;return tok; }
static Token errorToken(const char* msg){ Token tok;tok.type=TOKEN_ERROR;tok.start=msg;tok.length=(int)strlen(msg);tok.line=scanner.line;return tok; }
static void skipWhitespace(){for(;;){char c=peek();switch(c){case ' ':case '\r':case '\t':advance();break;case '\n':scanner.line++;advance();break;case '/':if(peekNext()=='/'){while(peek()!='\n'&&!isAtEnd())advance();}else return;break;default:return;}}}
static TokenType checkKeyword(int start,int len,const char* rest,TokenType t){if(scanner.current-scanner.start==start+len&&memcmp(scanner.start+start,rest,len)==0)return t;return TOKEN_IDENTIFIER;}
static TokenType identifierType(){
  switch(scanner.start[0]){
    case 'a':return checkKeyword(1,2,"nd",TOKEN_AND);
    case 'c':return checkKeyword(1,4,"lass",TOKEN_CLASS);
    case 'e':return checkKeyword(1,3,"lse",TOKEN_ELSE);
    case 'f':if(scanner.current-scanner.start>1)switch(scanner.start[1]){case 'a':return checkKeyword(2,3,"lse",TOKEN_FALSE);case 'o':return checkKeyword(2,1,"r",TOKEN_FOR);case 'u':return checkKeyword(2,1,"n",TOKEN_FUN);}break;
    case 'i':return checkKeyword(1,1,"f",TOKEN_IF);
    case 'n':return checkKeyword(1,2,"il",TOKEN_NIL);
    case 'o':return checkKeyword(1,1,"r",TOKEN_OR);
    case 'p':return checkKeyword(1,4,"rint",TOKEN_PRINT);
    case 'r':return checkKeyword(1,5,"eturn",TOKEN_RETURN);
    case 's':return checkKeyword(1,4,"uper",TOKEN_SUPER);
    case 't':if(scanner.current-scanner.start>1)switch(scanner.start[1]){case 'h':return checkKeyword(2,2,"is",TOKEN_THIS);case 'r':return checkKeyword(2,2,"ue",TOKEN_TRUE);}break;
    case 'v':return checkKeyword(1,2,"ar",TOKEN_VAR);
    case 'w':return checkKeyword(1,4,"hile",TOKEN_WHILE);
  }
  return TOKEN_IDENTIFIER;
}
static Token identifier(){ while(isAlpha(peek())||isDigit(peek()))advance();return makeToken(identifierType()); }
static Token number(){ while(isDigit(peek()))advance();if(peek()=='.'&&isDigit(peekNext())){advance();while(isDigit(peek()))advance();}return makeToken(TOKEN_NUMBER); }
static Token string_(){ while(peek()!='"'&&!isAtEnd()){if(peek()=='\n')scanner.line++;advance();}if(isAtEnd())return errorToken("Unterminated string.");advance();return makeToken(TOKEN_STRING); }
Token scanToken(){
  skipWhitespace(); scanner.start=scanner.current;
  if(isAtEnd())return makeToken(TOKEN_EOF);
  char c=advance();
  if(isAlpha(c))return identifier();
  if(isDigit(c))return number();
  switch(c){
    case '(':return makeToken(TOKEN_LEFT_PAREN);case ')':return makeToken(TOKEN_RIGHT_PAREN);
    case '{':return makeToken(TOKEN_LEFT_BRACE);case '}':return makeToken(TOKEN_RIGHT_BRACE);
    case ';':return makeToken(TOKEN_SEMICOLON);case ',':return makeToken(TOKEN_COMMA);
    case '.':return makeToken(TOKEN_DOT);case '-':return makeToken(TOKEN_MINUS);
    case '+':return makeToken(TOKEN_PLUS);case '/':return makeToken(TOKEN_SLASH);
    case '*':return makeToken(TOKEN_STAR);
    case '!':return makeToken(match('=')?TOKEN_BANG_EQUAL:TOKEN_BANG);
    case '=':return makeToken(match('=')?TOKEN_EQUAL_EQUAL:TOKEN_EQUAL);
    case '<':return makeToken(match('=')?TOKEN_LESS_EQUAL:TOKEN_LESS);
    case '>':return makeToken(match('=')?TOKEN_GREATER_EQUAL:TOKEN_GREATER);
    case '"':return string_();
  }
  return errorToken("Unexpected character.");
}
