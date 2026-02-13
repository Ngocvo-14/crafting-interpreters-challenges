package com.craftinginterpreters.lox;

import java.util.List;

public class InterpreterTest {

    private static final Interpreter interpreter = new Interpreter();
    private static int passed = 0;
    private static int failed = 0;

    public static void main(String[] args) {
        System.out.println("=== Chapter 7 Challenge Tests ===\n");

        // --- Normal operations (should still work) ---
        System.out.println("--- Basic Operations ---");
        testResult("2 + 3", "5");
        testResult("10 - 4", "6");
        testResult("3 * 4", "12");
        testResult("6 / 2", "3");
        testResult("1 == 1", "true");
        testResult("1 != 2", "true");
        testResult("!true", "false");
        testResult("-5", "-5");
        testResult("(2 + 3) * 4", "20");

        // --- Challenge 1: String comparisons ---
        System.out.println("\n--- Challenge 1: String Comparisons ---");
        testResult("\"apple\" < \"banana\"", "true");
        testResult("\"banana\" < \"apple\"", "false");
        testResult("\"zebra\" >= \"ant\"", "true");
        testResult("\"abc\" == \"abc\"", "true");
        testResult("\"a\" < \"b\"", "true");
        testResult("\"b\" > \"a\"", "true");
        testResult("\"hello\" <= \"hello\"", "true");
        testResult("\"hello\" >= \"hello\"", "true");
        // Mixed type comparison should error
        testError("3 < \"pancake\"", "Operands must be numbers.");
        testError("\"hello\" > 5", "Operands must be numbers.");

        // --- Challenge 2: String coercion with + ---
        System.out.println("\n--- Challenge 2: String Coercion ---");
        testResult("\"scone\" + 4", "scone4");
        testResult("4 + \"scone\"", "4scone");
        testResult("true + \" story\"", "true story");
        testResult("\"value: \" + nil", "value: nil");
        testResult("\"hello\" + \" world\"", "hello world");
        testResult("\"pi is \" + 3.14", "pi is 3.14");
        testResult("\"count: \" + 0", "count: 0");

        // --- Challenge 3: Division by zero ---
        System.out.println("\n--- Challenge 3: Division by Zero ---");
        testError("6 / 0", "Division by zero.");
        testError("1 / 0", "Division by zero.");
        testError("0 / 0", "Division by zero.");
        testError("-5 / 0", "Division by zero.");
        testResult("6 / 3", "2");  // Normal division still works

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
        // Reset error flag before each parse
        Lox.hadError = false;
        Scanner scanner = new Scanner(source);
        List<Token> tokens = scanner.scanTokens();
        Parser parser = new Parser(tokens);
        return parser.parse();
    }

    // Test that an expression produces the expected string output
    private static void testResult(String source, String expected) {
        try {
            Expr expr = parse(source);
            if (expr == null) {
                System.out.println("  FAIL: " + source +
                    " => parse error (got null)");
                failed++;
                return;
            }
            Object result = interpreter.evaluate(expr);
            String output = interpreter.stringify(result);

            if (output.equals(expected)) {
                System.out.println("  PASS: " + source + " => " + output);
                passed++;
            } else {
                System.out.println("  FAIL: " + source +
                    " => expected \"" + expected +
                    "\" but got \"" + output + "\"");
                failed++;
            }
        } catch (Exception e) {
            System.out.println("  FAIL: " + source +
                " => unexpected error: " + e.getMessage());
            failed++;
        }
    }

    // Test that an expression throws a RuntimeError with expected message
    private static void testError(String source, String expectedMessage) {
        try {
            Expr expr = parse(source);
            if (expr == null) {
                System.out.println("  FAIL: " + source +
                    " => parse error (got null), expected RuntimeError");
                failed++;
                return;
            }
            Object result = interpreter.evaluate(expr);
            System.out.println("  FAIL: " + source +
                " => expected error but got \"" +
                interpreter.stringify(result) + "\"");
            failed++;
        } catch (RuntimeError e) {
            if (e.getMessage().equals(expectedMessage)) {
                System.out.println("  PASS: " + source +
                    " => error: " + e.getMessage());
                passed++;
            } else {
                System.out.println("  FAIL: " + source +
                    " => expected error \"" + expectedMessage +
                    "\" but got \"" + e.getMessage() + "\"");
                failed++;
            }
        } catch (Exception e) {
            System.out.println("  FAIL: " + source +
                " => wrong exception type: " +
                e.getClass().getName() + ": " + e.getMessage());
            failed++;
        }
    }
}
