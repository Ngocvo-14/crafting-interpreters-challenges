package com.craftinginterpreters.lox;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;


class Interpreter implements Expr.Visitor<Object>, Stmt.Visitor<Void> {

    // The global scope — visible everywhere
    final Environment globals = new Environment();
    private Environment environment = globals;
    private final Map<Expr, Integer> locals = new HashMap<>();
    private LoxClass currentClass = null;

    Interpreter() {
        // Built-in: clock() returns seconds since epoch (used in tests)
        globals.define("clock", new LoxCallable() {
            @Override public int arity() { return 0; }
            @Override public Object call(Interpreter interpreter, List<Object> arguments) {
                return (double) System.currentTimeMillis() / 1000.0;
            }
            @Override public String toString() { return "<native fn>"; }
        });
	
	globals.define("len", new LoxCallable() {
    @Override public int arity() { return 1; }
    @Override public Object call(Interpreter interpreter, List<Object> arguments) {
        Object arg = arguments.get(0);
        if (arg instanceof String) return (double)((String) arg).length();
        throw new RuntimeError(
            new Token(TokenType.IDENTIFIER, "len", null, 0),
            "Argument to len() must be a string.");
    }
    @Override public String toString() { return "<native fn>"; }
});
    }

    // ── Public API ─────────────────────────────────────────────────────────

    void interpret(List<Stmt> statements) {
        try {
            for (Stmt statement : statements) execute(statement);
        } catch (RuntimeError error) {
            Lox.runtimeError(error);
        }
    }

    void resolve(Expr expr, int depth) { locals.put(expr, depth); }

    // ── Statement execution ────────────────────────────────────────────────

    private void execute(Stmt stmt) { stmt.accept(this); }

    void executeBlock(List<Stmt> statements, Environment environment) {
        Environment previous = this.environment;
        try {
            this.environment = environment;
            for (Stmt statement : statements) execute(statement);
        } finally {
            this.environment = previous;
        }
    }

    @Override
    public Void visitBlockStmt(Stmt.Block stmt) {
        executeBlock(stmt.statements, new Environment(environment));
        return null;
    }

     
        @Override
    public Void visitBreakStmt(Stmt.Break stmt) {
        throw new BreakException();
    }

    @Override
public Void visitClassStmt(Stmt.Class stmt) {
    Object superclass = null;
    if (stmt.superclass != null) {
        superclass = evaluate(stmt.superclass);
        if (!(superclass instanceof LoxClass))
            throw new RuntimeError(stmt.superclass.name, "Superclass must be a class.");
    }
    environment.define(stmt.name.lexeme, null);
    if (stmt.superclass != null) {
        environment = new Environment(environment);
        environment.define("super", superclass);
    }

    // Instance methods (unchanged)
    Map<String, LoxFunction> methods = new HashMap<>();
    for (Stmt.Function method : stmt.methods) {
        LoxFunction function = new LoxFunction(method, environment,
                method.name.lexeme.equals("init"));
        methods.put(method.name.lexeme, function);
    }

    // NEW: Static (class) methods
    Map<String, LoxFunction> classMethods = new HashMap<>();
    for (Stmt.Function method : stmt.classMethods) {
        LoxFunction function = new LoxFunction(method, environment, false);
        classMethods.put(method.name.lexeme, function);
    }
// Evaluate mixins
List<LoxClass> mixins = new ArrayList<>();
for (Expr.Variable mixinExpr : stmt.mixins) {
    Object mixin = evaluate(mixinExpr);
    if (!(mixin instanceof LoxClass))
        throw new RuntimeError(mixinExpr.name, "Mixin must be a class.");
    mixins.add((LoxClass) mixin);
}

    LoxClass klass = new LoxClass(stmt.name.lexeme, (LoxClass) superclass,
    methods, classMethods, mixins);
    if (stmt.superclass != null) environment = environment.enclosing;
    environment.assign(stmt.name, klass);
    return null;
    }

    @Override
    public Void visitExpressionStmt(Stmt.Expression stmt) {
        evaluate(stmt.expression);
        return null;
    }

    @Override
    public Void visitFunctionStmt(Stmt.Function stmt) {
        LoxFunction function = new LoxFunction(stmt, environment, false);
        environment.define(stmt.name.lexeme, function);
        return null;
    }

    @Override
    public Void visitIfStmt(Stmt.If stmt) {
        if (isTruthy(evaluate(stmt.condition))) {
            execute(stmt.thenBranch);
        } else if (stmt.elseBranch != null) {
            execute(stmt.elseBranch);
        }
        return null;
    }

    @Override
    public Void visitPrintStmt(Stmt.Print stmt) {
        Object value = evaluate(stmt.expression);
        System.out.println(stringify(value));
        return null;
    }

    @Override
    public Void visitReturnStmt(Stmt.Return stmt) {
        Object value = null;
        if (stmt.value != null) value = evaluate(stmt.value);
        throw new Return(value);
    }

    @Override
    public Void visitVarStmt(Stmt.Var stmt) {
        Object value = null;
        if (stmt.initializer != null) value = evaluate(stmt.initializer);
        environment.define(stmt.name.lexeme, value);
        return null;
    }

        @Override
    public Void visitWhileStmt(Stmt.While stmt) {
        try {
            while (isTruthy(evaluate(stmt.condition))) {
                execute(stmt.body);
            }
        } catch (BreakException e) {
            // break executed — exit the loop and continue past it
        }
        return null;
    }

    // ── Expression evaluation ──────────────────────────────────────────────

    private Object evaluate(Expr expr) { return expr.accept(this); }

    @Override
    public Object visitLambdaExpr(Expr.Lambda expr) {
        // Wrap the lambda in a synthetic Stmt.Function so LoxFunction can hold it
        Token syntheticName = new Token(TokenType.IDENTIFIER, "<lambda>", null, 0);
        Stmt.Function declaration = new Stmt.Function(syntheticName, expr.params, expr.body);
        return new LoxFunction(declaration, environment, false);
    }

    @Override
    public Object visitAssignExpr(Expr.Assign expr) {
        Object value = evaluate(expr.value);
        Integer distance = locals.get(expr);
        if (distance != null) environment.assignAt(distance, expr.name, value);
        else globals.assign(expr.name, value);
        return value;
    }

    @Override
    public Object visitBinaryExpr(Expr.Binary expr) {
        Object left  = evaluate(expr.left);
        Object right = evaluate(expr.right);
        switch (expr.operator.type) {
            case MINUS:         checkNumberOperands(expr.operator, left, right); return (double) left - (double) right;
            case SLASH:         checkNumberOperands(expr.operator, left, right);
                                if ((double) right == 0) throw new RuntimeError(expr.operator, "Division by zero.");
                                return (double) left / (double) right;
            case STAR:          checkNumberOperands(expr.operator, left, right); return (double) left * (double) right;
            case PLUS:
                if (left instanceof Double && right instanceof Double) return (double) left + (double) right;
                if (left instanceof String || right instanceof String) return stringify(left) + stringify(right);
                throw new RuntimeError(expr.operator, "Operands must be two numbers or two strings.");
            case GREATER:       checkNumberOperands(expr.operator, left, right); return (double) left > (double) right;
            case GREATER_EQUAL: checkNumberOperands(expr.operator, left, right); return (double) left >= (double) right;
            case LESS:          checkNumberOperands(expr.operator, left, right); return (double) left < (double) right;
            case LESS_EQUAL:    checkNumberOperands(expr.operator, left, right); return (double) left <= (double) right;
            case BANG_EQUAL:    return !isEqual(left, right);
            case EQUAL_EQUAL:   return isEqual(left, right);
        }
        return null;
    }

    @Override
    public Object visitCallExpr(Expr.Call expr) {
        Object callee = evaluate(expr.callee);
        List<Object> arguments = new ArrayList<>();
        for (Expr argument : expr.arguments) arguments.add(evaluate(argument));
        if (!(callee instanceof LoxCallable))
            throw new RuntimeError(expr.paren, "Can only call functions and classes.");
        LoxCallable function = (LoxCallable) callee;
        if (arguments.size() != function.arity())
            throw new RuntimeError(expr.paren,
                    "Expected " + function.arity() + " arguments but got " + arguments.size() + ".");
        return function.call(this, arguments);
    }

    @Override
public Object visitGetExpr(Expr.Get expr) {
    Object object = evaluate(expr.object);

    if (object instanceof LoxInstance) {
        Object result = ((LoxInstance) object).get(expr.name);
        // If result is a getter, execute it immediately
        if (result instanceof LoxFunction &&
            ((LoxFunction) result).isGetter()) {
            result = ((LoxFunction) result).call(this, null);
        }
        return result;
    }

    // NEW: Math.square — object is LoxClass, not LoxInstance
    if (object instanceof LoxClass) {
        LoxFunction classMethod = ((LoxClass) object).findClassMethod(expr.name.lexeme);
        if (classMethod != null) return classMethod;
        throw new RuntimeError(expr.name,
            "Undefined static property '" + expr.name.lexeme + "'.");
    }

    throw new RuntimeError(expr.name, "Only instances have properties.");
}

    @Override
    public Object visitGroupingExpr(Expr.Grouping expr) { return evaluate(expr.expression); }

    @Override
    public Object visitLiteralExpr(Expr.Literal expr) { return expr.value; }

    @Override
    public Object visitLogicalExpr(Expr.Logical expr) {
        Object left = evaluate(expr.left);
        if (expr.operator.type == TokenType.OR) {
            if (isTruthy(left)) return left;
        } else {
            if (!isTruthy(left)) return left;
        }
        return evaluate(expr.right);
    }

    @Override
    public Object visitSetExpr(Expr.Set expr) {
        Object object = evaluate(expr.object);
        if (!(object instanceof LoxInstance))
            throw new RuntimeError(expr.name, "Only instances have fields.");
        Object value = evaluate(expr.value);
        ((LoxInstance) object).set(expr.name, value);
        return value;
    }

    @Override
    public Object visitSuperExpr(Expr.Super expr) {
        int distance = locals.get(expr);
        LoxClass superclass = (LoxClass) environment.getAt(distance, "super");
        LoxInstance object  = (LoxInstance) environment.getAt(distance - 1, "this");
        LoxFunction method  = superclass.findMethod(expr.method.lexeme);
        if (method == null)
            throw new RuntimeError(expr.method, "Undefined property '" + expr.method.lexeme + "'.");
        return method.bind(object);
    }

    @Override
    public Object visitThisExpr(Expr.This expr) { return lookUpVariable(expr.keyword, expr); }

    @Override
    public Object visitUnaryExpr(Expr.Unary expr) {
        Object right = evaluate(expr.right);
        switch (expr.operator.type) {
            case BANG:  return !isTruthy(right);
            case MINUS: checkNumberOperand(expr.operator, right); return -(double) right;
        }
        return null;
    }

    @Override
    public Object visitVariableExpr(Expr.Variable expr) { return lookUpVariable(expr.name, expr); }

    private Object lookUpVariable(Token name, Expr expr) {
        Integer distance = locals.get(expr);
        return (distance != null) ? environment.getAt(distance, name.lexeme) : globals.get(name);
    }

@Override
public Object visitInnerExpr(Expr.Inner expr) {
    LoxClass executingClass = null;
    String methodName = null;
    LoxInstance instance = null;

    Environment env = environment;
    while (env != null) {
        if (executingClass == null && env.values.containsKey("inner_class"))
            executingClass = (LoxClass) env.values.get("inner_class");
        if (methodName == null && env.values.containsKey("inner_method"))
            methodName = (String) env.values.get("inner_method");
        if (instance == null && env.values.containsKey("this"))
            instance = (LoxInstance) env.values.get("this");
        if (executingClass != null && methodName != null && instance != null) break;
        env = env.enclosing;
    }

    if (executingClass == null || methodName == null || instance == null) return null;

    LoxFunction method = findInnerMethod(instance.getKlass(), executingClass, methodName);
    if (method == null) {
    return new LoxCallable() {
        @Override public int arity() { return 0; }
        @Override public Object call(Interpreter interpreter, List<Object> arguments) {
            return null; // inner() does nothing
        }
        @Override public String toString() { return "<inner>"; }
    };
}
// Return the bound method as a callable so inner() works
return method.bindWithInner(instance, executingClass, methodName);
}

// Walk down from instanceClass to find the method BELOW executingClass
private LoxFunction findInnerMethod(LoxClass instanceClass, 
                                     LoxClass executingClass, 
                                     String name) {
    // Build the chain from top (supermost) to instanceClass
    List<LoxClass> chain = new ArrayList<>();
    for (LoxClass c = instanceClass; c != null; c = c.superclass) {
        chain.add(0, c); // prepend so index 0 = top of hierarchy
    }
    // Find executingClass in chain, then look for name in classes below it
    boolean found = false;
    for (LoxClass c : chain) {
        if (found) {
            // Check if this class directly defines the method
            LoxFunction m = c.findOwnMethod(name);
            if (m != null) return m;
        }
        if (c == executingClass) found = true;
    }
    return null;
}

    // ── Helpers ────────────────────────────────────────────────────────────

    private boolean isTruthy(Object object) {
        if (object == null)          return false;
        if (object instanceof Boolean) return (boolean) object;
        return true;
    }

    private boolean isEqual(Object a, Object b) {
        if (a == null && b == null) return true;
        if (a == null) return false;
        return a.equals(b);
    }

    private void checkNumberOperand(Token operator, Object operand) {
        if (operand instanceof Double) return;
        throw new RuntimeError(operator, "Operand must be a number.");
    }

    private void checkNumberOperands(Token operator, Object left, Object right) {
        if (left instanceof Double && right instanceof Double) return;
        throw new RuntimeError(operator, "Operands must be numbers.");
    }

    String stringify(Object object) {
        if (object == null) return "nil";
        if (object instanceof Double) {
            String text = object.toString();
            if (text.endsWith(".0")) text = text.substring(0, text.length() - 2);
            return text;
        }
        return object.toString();
    }
}