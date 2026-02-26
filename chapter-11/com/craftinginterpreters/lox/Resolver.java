package com.craftinginterpreters.lox;

import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Stack;

class Resolver implements Expr.Visitor<Void>, Stmt.Visitor<Void> {
    private final Interpreter interpreter;
    private final Stack<Map<String, Variable>> scopes = new Stack<>();
    private final Stack<Integer> scopeIndexCounters = new Stack<>();
    private FunctionType currentFunction = FunctionType.NONE;
    private ClassType currentClass = ClassType.NONE;

    private enum FunctionType { NONE, FUNCTION, INITIALIZER, METHOD }
    private enum ClassType    { NONE, CLASS, SUBCLASS }
    private enum VariableState { DECLARED, DEFINED, READ }

    Resolver(Interpreter interpreter) { this.interpreter = interpreter; }

    // ── Public entry point ─────────────────────────────────────────────────
    void resolve(List<Stmt> statements) {
        for (Stmt stmt : statements) resolve(stmt);
    }

    private void resolve(Stmt stmt) { stmt.accept(this); }
    private void resolve(Expr expr) { expr.accept(this); }

    private void beginScope() {
    	scopes.push(new HashMap<>());
    	scopeIndexCounters.push(0);
    }

    private void endScope() {
    	Map<String, Variable> scope = scopes.pop();
    	scopeIndexCounters.pop();
    	for (Map.Entry<String, Variable> entry : scope.entrySet()) {
        	if (entry.getValue().state == VariableState.DEFINED) {
            		Lox.error(entry.getValue().name, "Local variable is not used.");
        	}
    	}
    }

    private void declare(Token name) {
    	if (scopes.isEmpty()) return;
    	Map<String, Variable> scope = scopes.peek();
    	if (scope.containsKey(name.lexeme)) {
        	Lox.error(name, "Already a variable with this name in this scope.");
    }
    	int index = scopeIndexCounters.peek();
    	scopeIndexCounters.set(scopeIndexCounters.size() - 1, index + 1);
    	scope.put(name.lexeme, new Variable(name, VariableState.DECLARED, index));
    }

    private void define(Token name) {
    	if (scopes.isEmpty()) return;
    	scopes.peek().get(name.lexeme).state = VariableState.DEFINED;
    }

    private void resolveLocal(Expr expr, Token name, boolean isRead) {
    	for (int i = scopes.size() - 1; i >= 0; i--) {
        	if (scopes.get(i).containsKey(name.lexeme)) {
            		Variable var = scopes.get(i).get(name.lexeme);
            		interpreter.resolve(expr, scopes.size() - 1 - i, var.index);
            		if (isRead) var.state = VariableState.READ;
            		return;
        	}
    	}
    }

    private void resolveFunction(Stmt.Function function, FunctionType type){
        FunctionType enclosingFunction = currentFunction;
        currentFunction = type;
        beginScope();
        for (Token param : function.params) {
            declare(param);
            define(param);
        }
        resolve(function.body);
        endScope();
        currentFunction = enclosingFunction;
    }


    private static class Variable {
    	final Token name;
    	VariableState state;
	final int index;

    	private Variable(Token name, VariableState state, int index) {
        	this.name = name;
        	this.state = state;
		this.index = index;
    	}
    }
    // ── Stmt visitors ──────────────────────────────────────────────────────

    @Override
    public Void visitBlockStmt(Stmt.Block stmt) {
        beginScope();
        resolve(stmt.statements);
        endScope();
        return null;
    }

    @Override
    public Void visitBreakStmt(Stmt.Break stmt) {
        return null;
    }

    @Override
    public Void visitClassStmt(Stmt.Class stmt) {
        ClassType enclosingClass = currentClass;
        currentClass = ClassType.CLASS;

        declare(stmt.name);
        define(stmt.name);

        if (stmt.superclass != null &&
                stmt.name.lexeme.equals(stmt.superclass.name.lexeme)) {
            Lox.error(stmt.superclass.name, "A class can't inherit from itself.");
        }

        if (stmt.superclass != null) {
            currentClass = ClassType.SUBCLASS;
            resolve(stmt.superclass);
        }

        if (stmt.superclass != null) {
            beginScope();
            scopes.peek().put("super", new Variable(null, VariableState.READ, 0));
        }

        beginScope();
        scopes.peek().put("this", new Variable(null, VariableState.READ, 0));

        for (Stmt.Function method : stmt.methods) {
            FunctionType declaration = method.name.lexeme.equals("init")
                    ? FunctionType.INITIALIZER : FunctionType.METHOD;
            resolveFunction(method, declaration);
        }

        endScope();
        if (stmt.superclass != null) endScope();
        currentClass = enclosingClass;
        return null;
    }

    @Override
    public Void visitExpressionStmt(Stmt.Expression stmt) {
        resolve(stmt.expression);
        return null;
    }

    @Override
    public Void visitFunctionStmt(Stmt.Function stmt) {
        declare(stmt.name);
        define(stmt.name);
        resolveFunction(stmt, FunctionType.FUNCTION);
        return null;
    }

    @Override
    public Void visitIfStmt(Stmt.If stmt) {
        resolve(stmt.condition);
        resolve(stmt.thenBranch);
        if (stmt.elseBranch != null) resolve(stmt.elseBranch);
        return null;
    }

    @Override
    public Void visitPrintStmt(Stmt.Print stmt) {
        resolve(stmt.expression);
        return null;
    }

    @Override
    public Void visitReturnStmt(Stmt.Return stmt) {
        if (currentFunction == FunctionType.NONE)
            Lox.error(stmt.keyword, "Can't return from top-level code.");
        if (stmt.value != null) {
            if (currentFunction == FunctionType.INITIALIZER)
                Lox.error(stmt.keyword, "Can't return a value from an initializer.");
            resolve(stmt.value);
        }
        return null;
    }

    @Override
    public Void visitVarStmt(Stmt.Var stmt) {
    	declare(stmt.name);
    	if (stmt.initializer != null) resolve(stmt.initializer);
    	define(stmt.name);
    	return null;
    }

    @Override
    public Void visitWhileStmt(Stmt.While stmt) {
        resolve(stmt.condition);
        resolve(stmt.body);
        return null;
    }

    // ── Expr visitors ──────────────────────────────────────────────────────
    @Override
    public Void visitBinaryExpr(Expr.Binary expr) {
        resolve(expr.left);
        resolve(expr.right);
        return null;
    }

    @Override
    public Void visitCallExpr(Expr.Call expr) {
        resolve(expr.callee);
        for (Expr argument : expr.arguments) resolve(argument);
        return null;
    }

    @Override
    public Void visitGetExpr(Expr.Get expr) {
        resolve(expr.object);
        return null;
    }

    @Override
    public Void visitGroupingExpr(Expr.Grouping expr) {
        resolve(expr.expression);
        return null;
    }

    
    @Override
    public Void visitLambdaExpr(Expr.Lambda expr) {
        FunctionType enclosingFunction = currentFunction;
        currentFunction = FunctionType.FUNCTION;
        beginScope();
        for (Token param : expr.params) {
            declare(param);
            define(param);
        }
        resolve(expr.body);
        endScope();
        currentFunction = enclosingFunction;
        return null;
    }

    @Override
    public Void visitLiteralExpr(Expr.Literal expr) {
        return null;
    }

    @Override
    public Void visitLogicalExpr(Expr.Logical expr) {
        resolve(expr.left);
        resolve(expr.right);
        return null;
    }

    @Override
    public Void visitSetExpr(Expr.Set expr) {
        resolve(expr.value);
        resolve(expr.object);
        return null;
    }

    @Override
    public Void visitSuperExpr(Expr.Super expr) {
    	if (currentClass == ClassType.NONE) {
        	Lox.error(expr.keyword, "Can't use 'super' outside of a class.");
    	} else if (currentClass != ClassType.SUBCLASS) {
        	Lox.error(expr.keyword, "Can't use 'super' in a class with no superclass.");
    	}
    	resolveLocal(expr, expr.keyword, false);  // ← add false
    	return null;
    }

    @Override
    public Void visitThisExpr(Expr.This expr) {
    	if (currentClass == ClassType.NONE) {
        	Lox.error(expr.keyword, "Can't use 'this' outside of a class.");
        	return null;
    	}
    	resolveLocal(expr, expr.keyword, false);  // ← add false
    	return null;
    }

    @Override
    public Void visitUnaryExpr(Expr.Unary expr) {
        resolve(expr.right);
        return null;
    }

    @Override
    public Void visitVariableExpr(Expr.Variable expr) {
    	if (!scopes.isEmpty() &&
        	scopes.peek().containsKey(expr.name.lexeme) &&
        	scopes.peek().get(expr.name.lexeme).state == VariableState.DECLARED) {
        	Lox.error(expr.name, "Can't read local variable in its own initializer.");
    	}
    	resolveLocal(expr, expr.name, true);
    	return null;
     }

     @Override
     public Void visitAssignExpr(Expr.Assign expr) {
    	resolve(expr.value);
    	resolveLocal(expr, expr.name, false);
    	return null;
     }
}