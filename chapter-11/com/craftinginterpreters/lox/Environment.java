package com.craftinginterpreters.lox;

import java.util.ArrayList;  
import java.util.HashMap;
import java.util.List;        
import java.util.Map;

class Environment {
    final Environment enclosing;
    private final Map<String, Object> values = new HashMap<>();
    final List<Object> slots = new ArrayList<>();

    Environment() { enclosing = null; }
    Environment(Environment enclosing) { this.enclosing = enclosing; }

    Object get(Token name) {
        if (values.containsKey(name.lexeme)) return values.get(name.lexeme);
        if (enclosing != null) return enclosing.get(name);
        throw new RuntimeError(name, "Undefined variable '" + name.lexeme + "'.");
    }
    // New index-based getter:
    Object getAt(int distance, int index) {
    	return ancestor(distance).slots.get(index);
    }

    // New index-based setter:
    void assignAt(int distance, int index, Object value) {
    	ancestor(distance).slots.set(index, value);
    }

    void assign(Token name, Object value) {
        if (values.containsKey(name.lexeme)) { values.put(name.lexeme, value); return; }
        if (enclosing != null) { enclosing.assign(name, value); return; }
        throw new RuntimeError(name, "Undefined variable '" + name.lexeme + "'.");
    }

    void define(String name, Object value) {
    	values.put(name, value);
    	slots.add(value);   
    }  

    // Needed by Interpreter for variable resolution (chapters 11+)
    Environment ancestor(int distance) {
        Environment environment = this;
        for (int i = 0; i < distance; i++) environment = environment.enclosing;
        return environment;
    }

    Object getAt(int distance, String name) {
        return ancestor(distance).values.get(name);
    }

    void assignAt(int distance, Token name, Object value) {
        ancestor(distance).values.put(name.lexeme, value);
    }
}