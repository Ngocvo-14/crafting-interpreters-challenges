package com.craftinginterpreters.lox;

class Return extends RuntimeException {
    final Object value;
    Return(Object value) {
        super(null, null, true, false);
        this.value = value;
    }
}