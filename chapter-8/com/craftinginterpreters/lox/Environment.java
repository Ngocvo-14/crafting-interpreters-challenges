package com.craftinginterpreters.lox;

import java.util.HashMap;
import java.util.Map;

class Environment {
  final Environment enclosing;
  private final Map<String, Object> values = new HashMap<>();

  // Sentinel: marks a declared-but-never-initialized variable
  private static final Object UNINITIALIZED = new Object();

  Environment() {
    enclosing = null;
  }

  Environment(Environment enclosing) {
    this.enclosing = enclosing;
  }

  // Called when var has an initializer: var a = 5;
  void define(String name, Object value) {
    values.put(name, value);
  }

  // Called when var has NO initializer: var a;
  // Challenge 2: accessing this before assignment = runtime error
  void defineUninitialized(String name) {
    values.put(name, UNINITIALIZED);
  }

  Object get(Token name) {
    if (values.containsKey(name.lexeme)) {
      Object value = values.get(name.lexeme);

      // Challenge 2: catch access before initialization
      if (value == UNINITIALIZED) {
        throw new RuntimeError(name,
            "Variable '" + name.lexeme + "' has not been initialized.");
      }

      return value;
    }

    if (enclosing != null) return enclosing.get(name);

    throw new RuntimeError(name,
        "Undefined variable '" + name.lexeme + "'.");
  }

  void assign(Token name, Object value) {
    if (values.containsKey(name.lexeme)) {
      values.put(name.lexeme, value);
      return;
    }

    if (enclosing != null) {
      enclosing.assign(name, value);
      return;
    }

    throw new RuntimeError(name,
        "Undefined variable '" + name.lexeme + "'.");
  }
}