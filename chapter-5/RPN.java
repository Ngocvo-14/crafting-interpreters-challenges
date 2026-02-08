package com.craftinginterpreters.lox;

/**
 * Chapter 5 Challenge 3: RPN Printer
 * Converts expressions to Reverse Polish Notation (RPN)
 * 
 * Example: (1 + 2) * (4 - 3) becomes "1 2 + 4 3 - *"
 */
class RPN implements Expr.Visitor<String> {
    
    /**
     * Convert an expression to RPN string
     */
    String print(Expr expr) {
        return expr.accept(this);
    }

    @Override
    public String visitBinaryExpr(Expr.Binary expr) {
        // In RPN: left operand, right operand, then operator
        return expr.left.accept(this) + " " +
               expr.right.accept(this) + " " +
               expr.operator.lexeme;
    }

    @Override
    public String visitGroupingExpr(Expr.Grouping expr) {
        // Grouping doesn't affect RPN - parentheses are implicit in the order
        return expr.expression.accept(this);
    }

    @Override
    public String visitLiteralExpr(Expr.Literal expr) {
        if (expr.value == null) return "nil";
        return expr.value.toString();
    }

    @Override
    public String visitUnaryExpr(Expr.Unary expr) {
        // In RPN: operand comes first, then operator
        return expr.right.accept(this) + " " +
               expr.operator.lexeme;
    }

    /**
     * Test the RPN converter with example expressions
     */
    public static void main(String[] args) {
        System.out.println("=== RPN Converter Tests ===\n");

        // Test 1: (1 + 2) * (4 - 3)
        // Expected: 1 2 + 4 3 - *
        Expr expression1 = new Expr.Binary(
            new Expr.Grouping(
                new Expr.Binary(
                    new Expr.Literal(1),
                    new Token(TokenType.PLUS, "+", null, 1),
                    new Expr.Literal(2)
                )
            ),
            new Token(TokenType.STAR, "*", null, 1),
            new Expr.Grouping(
                new Expr.Binary(
                    new Expr.Literal(4),
                    new Token(TokenType.MINUS, "-", null, 1),
                    new Expr.Literal(3)
                )
            )
        );
        
        System.out.println("Test 1: (1 + 2) * (4 - 3)");
        System.out.println("RPN: " + new RPN().print(expression1));
        System.out.println("Expected: 1 2 + 4 3 - *\n");

        // Test 2: -123 * 45.67
        // Expected: 123 - 45.67 *
        Expr expression2 = new Expr.Binary(
            new Expr.Unary(
                new Token(TokenType.MINUS, "-", null, 1),
                new Expr.Literal(123)
            ),
            new Token(TokenType.STAR, "*", null, 1),
            new Expr.Literal(45.67)
        );
        
        System.out.println("Test 2: -123 * 45.67");
        System.out.println("RPN: " + new RPN().print(expression2));
        System.out.println("Expected: 123 - 45.67 *\n");

        // Test 3: 1 - (2 * 3) < 4 == false
        // Expected: 1 2 3 * - 4 < false ==
        Expr expression3 = new Expr.Binary(
            new Expr.Binary(
                new Expr.Binary(
                    new Expr.Literal(1),
                    new Token(TokenType.MINUS, "-", null, 1),
                    new Expr.Grouping(
                        new Expr.Binary(
                            new Expr.Literal(2),
                            new Token(TokenType.STAR, "*", null, 1),
                            new Expr.Literal(3)
                        )
                    )
                ),
                new Token(TokenType.LESS, "<", null, 1),
                new Expr.Literal(4)
            ),
            new Token(TokenType.EQUAL_EQUAL, "==", null, 1),
            new Expr.Literal(false)
        );
        
        System.out.println("Test 3: 1 - (2 * 3) < 4 == false");
        System.out.println("RPN: " + new RPN().print(expression3));
        System.out.println("Expected: 1 2 3 * - 4 < false ==");
    }
}