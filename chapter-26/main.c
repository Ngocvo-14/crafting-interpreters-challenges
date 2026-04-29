#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "vm.h"
static char* readFile(const char* path) {
  FILE* f=fopen(path,"rb"); if(!f){fprintf(stderr,"Could not open \"%s\".\n",path);exit(74);}
  fseek(f,0L,SEEK_END); size_t sz=(size_t)ftell(f); rewind(f);
  char* buf=(char*)malloc(sz+1); if(!buf){fprintf(stderr,"Not enough memory.\n");exit(74);}
  size_t n=fread(buf,sizeof(char),sz,f); buf[n]='\0'; fclose(f); return buf;
}
static void runFile(const char* path) {
  char* src=readFile(path); InterpretResult r=interpret(src); free(src);
  if(r==INTERPRET_COMPILE_ERROR)exit(65);
  if(r==INTERPRET_RUNTIME_ERROR)exit(70);
}
static void repl() {
  char line[1024];
  for(;;){printf("> ");if(!fgets(line,sizeof(line),stdin)){printf("\n");break;}interpret(line);}
}
int main(int argc,const char* argv[]){
  initVM();
  if(argc==1)repl();else if(argc==2)runFile(argv[1]);else{fprintf(stderr,"Usage: clox [path]\n");exit(64);}
  freeVM();return 0;
}
