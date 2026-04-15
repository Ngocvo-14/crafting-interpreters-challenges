#include <stdio.h>
#include "common.h"
#include "vm.h"
int main(void) {
  initVM();
  interpret("print 1 + 2;");
  interpret("{ val x = 42; print x; }");
  interpret("{ val x = 10; x = 20; }");
  interpret("{ val x; }");
  interpret("{ var a = a; }");
  interpret("{ var x = 1; var y = 2; print x + y; }");
  interpret("{ var a = 1; { var b = 2; print a + b; } }");
  freeVM();
  return 0;
}