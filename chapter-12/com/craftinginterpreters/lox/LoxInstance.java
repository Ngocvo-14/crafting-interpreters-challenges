package com.craftinginterpreters.lox;

import java.util.HashMap;
import java.util.Map;

class LoxInstance {
    private LoxClass klass;
    private final Map<String, Object> fields = new HashMap<>();

    LoxInstance(LoxClass klass) { this.klass = klass; }

    Object get(Token name) {
    // 1. Check instance fields first
    if (fields.containsKey(name.lexeme)) 
        return fields.get(name.lexeme);

    // 2. Check instance methods
    LoxFunction method = klass.findMethod(name.lexeme);
    if (method != null) return method.bind(this);

    // 3. NEW: Check static (class) methods
    LoxFunction classMethod = klass.findClassMethod(name.lexeme);
    if (classMethod != null) return classMethod;  // no bind — no 'this'

    throw new RuntimeError(name, 
        "Undefined property '" + name.lexeme + "'.");
}

    void set(Token name, Object value) { fields.put(name.lexeme, value); }

    @Override
    public String toString() { return klass.name + " instance"; }
}