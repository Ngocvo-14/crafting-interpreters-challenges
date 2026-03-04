package com.craftinginterpreters.lox;

import java.util.List;

class LoxFunction implements LoxCallable {
    private final Stmt.Function declaration;
    private final Environment closure;
    private final boolean isInitializer;

    LoxFunction(Stmt.Function declaration, Environment closure, boolean isInitializer) {
        this.declaration = declaration;
        this.closure = closure;
        this.isInitializer = isInitializer;
    }

    LoxFunction bind(LoxInstance instance) {
        Environment environment = new Environment(closure);
        environment.define("this", instance);
        return new LoxFunction(declaration, environment, isInitializer);
    }
LoxFunction bindWithInner(LoxInstance instance, LoxClass owningClass, String methodName) {
    Environment env = new Environment(closure);
    env.define("this", instance);
    env.define("inner_class", owningClass);
    env.define("inner_method", methodName);
    return new LoxFunction(declaration, env, isInitializer);
}

    @Override
public int arity() {
  if (declaration.params == null) return 0;  // Handle null for getters
  return declaration.params.size();
}

    @Override
public Object call(Interpreter interpreter,
                   List<Object> arguments) {
  Environment environment = new Environment(closure);
  
  // Check for null (getters have no parameters)
  if (declaration.params != null) {
    for (int i = 0; i < declaration.params.size(); i++) {
      environment.define(declaration.params.get(i).lexeme,
          arguments.get(i));
    }
  }

  try {
    interpreter.executeBlock(declaration.body, environment);
  } catch (Return returnValue) {
    if (isInitializer) return closure.getAt(0, "this");
    return returnValue.value;
  }

  if (isInitializer) return closure.getAt(0, "this");
  return null;
}

    @Override
    public String toString() { return "<fn " + declaration.name.lexeme + ">"; }
	boolean isGetter() {
    return declaration.params == null;
  }
}
	