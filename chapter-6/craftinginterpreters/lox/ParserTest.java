package com.craftinginterpreters.lox;

import java.util.List;

public class ParserTest {

    private static final AstPrinter printer = new AstPrinter();
    private static int passed = 0;
    private static int failed = 0;

    public static void main(String[] args) {
        System.out.println("=== Chapter 6 Challenge Tests ===\n");

        // --- Basic parsing (should still work) ---
        System.out.println("--- Basic Parsing ---");
        testParse("1 + 2", "(+ 1.0 2.0)");
        testParse("1 + 2 * 3", "(+ 1.0 (* 2.0 3.0))");
        testParse("(1 + 2) * 3", "(* (group (+ 1.0 2.0)) 3.0)");
        testParse("!true", "(! true)");
        testParse("-5", "(- 5.0)");
        testParse("1 == 2", "(== 1.0 2.0)");
        testParse("1 != 2", "(!= 1.0 2.0)");
        testParse("1 < 2", "(< 1.0 2.0)");
        testParse("1 >= 2", "(>= 1.0 2.0)");
        testParse("1 + 2 + 3", "(+ (+ 1.0 2.0) 3.0)");  // left-associative

        // --- Challenge 1: Comma operator ---
        System.out.println("\n--- Challenge 1: Comma Operator ---");
        // Comma has lowest precedence, left-associative
        testParse("1, 2", "(, 1.0 2.0)");
        testParse("1, 2, 3", "(, (, 1.0 2.0) 3.0)");  // left-associative
        testParse("1 + 2, 3 + 4", "(, (+ 1.0 2.0) (+ 3.0 4.0))");
        testParse("1, 2 + 3", "(, 1.0 (+ 2.0 3.0))");  // + binds tighter than ,

        // --- Challenge 2: Ternary operator ---
        System.out.println("\n--- Challenge 2: Ternary Operator ---");
        // Basic ternary
        testParse("true ? 1 : 2", "(?: true 1.0 2.0)");
        // Ternary with expressions
        testParse("1 + 2 ? 3 : 4", "(?: (+ 1.0 2.0) 3.0 4.0)");
        // Any expression allowed between ? and :
        testParse("true ? 1, 2 : 3", "(?: true (, 1.0 2.0) 3.0)");
        // Right-associative: a ? b : c ? d : e => a ? b : (c ? d : e)
        testParse("1 ? 2 : 3 ? 4 : 5", "(?: 1.0 2.0 (?: 3.0 4.0 5.0))");
        // Ternary with comparison
        testParse("1 == 1 ? 10 : 20", "(?: (== 1.0 1.0) 10.0 20.0)");
        // Comma lower precedence than ternary
        testParse("1, 2 ? 3 : 4", "(, 1.0 (?: 2.0 3.0 4.0))");

        // --- Challenge 3: Error productions ---
        System.out.println("\n--- Challenge 3: Error Productions ---");
        // Binary operator at start of expression (missing left operand)
        testParseError("+ 1");
        testParseError("* 2");
        testParseError("/ 3");
        testParseError("== 4");
        testParseError("!= 5");
        testParseError("> 6");
        testParseError(">= 7");
        testParseError("< 8");
        testParseError("<= 9");
        // Note: - is NOT an error since it's a valid unary operator
        testParse("-1", "(- 1.0)");  // unary minus is fine

        // --- Summary ---
        System.out.println("\n=== Results ===");
        System.out.println("Passed: " + passed);
        System.out.println("Failed: " + failed);
        System.out.println("Total:  " + (passed + failed));

        if (failed == 0) {
            System.out.println("\nAll tests passed!");
        } else {
            System.out.println("\nSome tests FAILED.");
        }
    }

    // Helper: scan + parse a source string into an expression
    private static Expr parse(String source) {
        Lox.hadError = false;
        Scanner scanner = new Scanner(source);
        List<Token> tokens = scanner.scanTokens();
        Parser parser = new Parser(tokens);
        return parser.parse();
    }

    // Test that an expression parses to the expected AST string
    private static void testParse(String source, String expectedAst) {
        try {
            Expr expr = parse(source);
            if (expr == null) {
                System.out.println("  FAIL: " + source +
                    " => parse returned null, expected \"" + expectedAst + "\"");
                failed++;
                return;
            }
            String ast = printer.print(expr);

            if (ast.equals(expectedAst)) {
                System.out.println("  PASS: " + source + " => " + ast);
                passed++;
            } else {
                System.out.println("  FAIL: " + source +
                    " => expected \"" + expectedAst +
                    "\" but got \"" + ast + "\"");
                failed++;
            }
        } catch (Exception e) {
            System.out.println("  FAIL: " + source +
                " => unexpected exception: " + e.getClass().getName() +
                ": " + e.getMessage());
            failed++;
        }
    }

    // Test that an expression triggers a parse error (error production)
    private static void testParseError(String source) {
        try {
            Lox.hadError = false;
            Scanner scanner = new Scanner(source);
            List<Token> tokens = scanner.scanTokens();
            Parser parser = new Parser(tokens);
            Expr expr = parser.parse();

            if (Lox.hadError) {
                System.out.println("  PASS: " + source +
                    " => correctly reported error");
                passed++;
            } else if (expr == null) {
                System.out.println("  PASS: " + source +
                    " => returned null (error)");
                passed++;
            } else {
                String ast = printer.print(expr);
                System.out.println("  FAIL: " + source +
                    " => expected error but parsed as \"" + ast + "\"");
                failed++;
            }
        } catch (Exception e) {
            // Any exception counts as detecting the error
            System.out.println("  PASS: " + source +
                " => error detected (" + e.getClass().getSimpleName() + ")");
            passed++;
        }
    }
}
