/**
 * Crafting Interpreters - Chapter 1, Challenge 2
 * Hello, World! in Java
 * 
 * This program demonstrates:
 * - Basic Java syntax
 * - String output with System.out.println()
 * - Main method entry point
 * - A simple loop for debugging practice
 * 
 * To compile: javac HelloWorld.java
 * To run: java HelloWorld
 */

public class HelloWorld {
    public static void main(String[] args) {
        String message = "Hello, World!";
       
        System.out.println(message);
        
        // Loop for additional debugging practice
        int count = 5;
        for (int i = 0; i < count; i++) {
            System.out.println("Iteration " + (i + 1));
        }
        
        System.out.println("Program completed!");
    }
}