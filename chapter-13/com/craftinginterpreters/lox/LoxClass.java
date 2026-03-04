package com.craftinginterpreters.lox;

import java.util.List;
import java.util.Map;

class LoxClass implements LoxCallable {
    final String name;
    final LoxClass superclass;
    private final Map<String, LoxFunction> methods;
    private final Map<String, LoxFunction> classMethods;
    private final List<LoxClass> mixins;

    LoxClass(String name, LoxClass superclass,
             Map<String, LoxFunction> methods,
             Map<String, LoxFunction> classMethods,
             List<LoxClass> mixins) {
        this.name = name;
        this.superclass = superclass;
        this.methods = methods;
        this.classMethods = classMethods;
        this.mixins = mixins;
    }

    LoxFunction findMethod(String name) {
        // 1. Own methods first
        if (methods.containsKey(name)) return methods.get(name);

        // 2. Search mixins in order
        for (LoxClass mixin : mixins) {
            LoxFunction m = mixin.findMethod(name);
            if (m != null) return m;
        }

        // 3. Superclass chain last
        if (superclass != null) return superclass.findMethod(name);

        return null;
    }

    LoxFunction findClassMethod(String name) {
        if (classMethods.containsKey(name)) return classMethods.get(name);
        return null;
    }

    LoxFunction findOwnMethod(String name) {
    	if (methods.containsKey(name)) return methods.get(name);
    	return null;
    }
LoxClass findOwnerFromTop(String name) {
    LoxClass owner = null;
    if (superclass != null) owner = superclass.findOwnerFromTop(name);
    if (owner != null) return owner;
    if (methods.containsKey(name)) return this;
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
        if (initializer != null)
            initializer.bind(instance).call(interpreter, arguments);
        return instance;
    }

    @Override
    public String toString() { return name; }
}