package com.craftinginterpreters.lox;

import java.util.List;
import java.util.Map;

class LoxClass implements LoxCallable {
    final String name;
    final LoxClass superclass;
    private final Map<String, LoxFunction> methods;
    private final Map<String, LoxFunction> classMethods; // NEW

    LoxClass(String name, LoxClass superclass,
             Map<String, LoxFunction> methods,
             Map<String, LoxFunction> classMethods) {    // NEW param
        this.name = name;
        this.superclass = superclass;
        this.methods = methods;
        this.classMethods = classMethods;                // NEW
    }

    LoxFunction findMethod(String name) {
        if (methods.containsKey(name)) return methods.get(name);
        if (superclass != null) return superclass.findMethod(name);
        return null;
    }

    // NEW: look up a static method by name
    LoxFunction findClassMethod(String name) {
        if (classMethods.containsKey(name)) return classMethods.get(name);
        return null;
    }

    @Override
    public int arity() {
        LoxFunction initializer = findMethod("init");
        return initializer == null ? 0 : initializer.arity();
    }

    @Override
    public Object call(Interpreter interpreter, List<Object> arguments) {
        LoxInstance instance = new LoxInstance(this);
        LoxFunction initializer = findMethod("init");
        if (initializer != null) initializer.bind(instance).call(interpreter, arguments);
        return instance;
    }

    @Override
    public String toString() { return name; }
}