package com.craftinginterpreters.lox;

import java.util.HashMap;
import java.util.Map;

class LoxInstance {
    private LoxClass klass;
    private final Map<String, Object> fields = new HashMap<>();

    LoxInstance(LoxClass klass) { this.klass = klass; }

    LoxClass getKlass() { return klass; }

    Object get(Token name) {
    if (fields.containsKey(name.lexeme))
        return fields.get(name.lexeme);

    // BETA: find method from TOP of hierarchy (superclass wins)
    LoxClass owner = klass.findOwnerFromTop(name.lexeme);
    if (owner != null) {
        LoxFunction method = owner.findOwnMethod(name.lexeme);
        return method.bindWithInner(this, owner, name.lexeme);
    }

    LoxFunction classMethod = klass.findClassMethod(name.lexeme);
    if (classMethod != null) return classMethod;

    throw new RuntimeError(name,
        "Undefined property '" + name.lexeme + "'.");
}

    void set(Token name, Object value) { fields.put(name.lexeme, value); }

    @Override
    public String toString() { return klass.name + " instance"; }
}
