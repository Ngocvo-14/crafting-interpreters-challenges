Expression: (-1 + 2) * 3 - -4
Result: 7

CALL TRACE:

compile()
  advance()                        → current = '('
  expression()
    parsePrecedence(PREC_ASSIGNMENT)
      advance()                    → consumed '(', previous = '('
      prefixRule = grouping        → rules['('].prefix
      grouping()
        expression()
          parsePrecedence(PREC_ASSIGNMENT)
            advance()              → consumed '-', previous = '-'
            prefixRule = unary     → rules['-'].prefix
            unary()
              operatorType = TOKEN_MINUS
              parsePrecedence(PREC_UNARY)
                advance()          → consumed '1', previous = '1'
                prefixRule = number
                number()
                  emitConstant(1.0)
                  → emits: OP_CONSTANT 0  '1'
                [current='+', PREC_TERM < PREC_UNARY → stop]
              emitByte(OP_NEGATE)
              → emits: OP_NEGATE

            [current='+', PREC_TERM >= PREC_ASSIGNMENT → loop]
            advance()              → consumed '+', previous = '+'
            infixRule = binary     → rules['+'].infix
            binary()
              operatorType = TOKEN_PLUS
              getRule('+'): precedence = PREC_TERM
              parsePrecedence(PREC_FACTOR)   ← PREC_TERM + 1
                advance()          → consumed '2', previous = '2'
                prefixRule = number
                number()
                  emitConstant(2.0)
                  → emits: OP_CONSTANT 1  '2'
                [current=')', PREC_NONE < PREC_FACTOR → stop]
              emitByte(OP_ADD)
              → emits: OP_ADD

            [current=')', PREC_NONE < PREC_ASSIGNMENT → stop]

        consume(TOKEN_RIGHT_PAREN) → consumed ')'
        [grouping emits nothing — purely syntactic]

      [current='*', PREC_FACTOR >= PREC_ASSIGNMENT → loop]
      advance()                    → consumed '*', previous = '*'
      infixRule = binary           → rules['*'].infix
      binary()
        operatorType = TOKEN_STAR
        getRule('*'): precedence = PREC_FACTOR
        parsePrecedence(PREC_UNARY)   ← PREC_FACTOR + 1
          advance()                → consumed '3', previous = '3'
          prefixRule = number
          number()
            emitConstant(3.0)
            → emits: OP_CONSTANT 2  '3'
          [current='-', PREC_TERM < PREC_UNARY → stop]
        emitByte(OP_MULTIPLY)
        → emits: OP_MULTIPLY

      [current='-', PREC_TERM >= PREC_ASSIGNMENT → loop]
      advance()                    → consumed '-', previous = '-'
      infixRule = binary           → rules['-'].infix
      binary()
        operatorType = TOKEN_MINUS
        getRule('-'): precedence = PREC_TERM
        parsePrecedence(PREC_FACTOR)  ← PREC_TERM + 1
          advance()                → consumed '-', previous = '-'
          prefixRule = unary       → rules['-'].prefix
          unary()
            operatorType = TOKEN_MINUS
            parsePrecedence(PREC_UNARY)
              advance()            → consumed '4', previous = '4'
              prefixRule = number
              number()
                emitConstant(4.0)
                → emits: OP_CONSTANT 3  '4'
              [current=EOF, PREC_NONE < PREC_UNARY → stop]
            emitByte(OP_NEGATE)
            → emits: OP_NEGATE
          [current=EOF, PREC_NONE < PREC_FACTOR → stop]
        emitByte(OP_SUBTRACT)
        → emits: OP_SUBTRACT

      [current=EOF, PREC_NONE < PREC_ASSIGNMENT → stop]

  consume(TOKEN_EOF)

endCompiler()
  emitReturn()
  → emits: OP_RETURN


BYTECODE:
OP_CONSTANT 0    '1'    ← the 1 inside (-1)
OP_NEGATE               ← negate to get -1
OP_CONSTANT 1    '2'    ← the 2
OP_ADD                  ← -1 + 2 = 1
OP_CONSTANT 2    '3'    ← the 3
OP_MULTIPLY             ← 1 * 3 = 3
OP_CONSTANT 3    '4'    ← the 4 in -4
OP_NEGATE               ← negate to get -4
OP_SUBTRACT             ← 3 - (-4) = 7
OP_RETURN


Note:
1. TOKEN_MINUS dual role: when '-' appears at the start of a subexpression,parsePrecedence finds rules['-'].prefix = unary and calls it. When '-' appears after a complete expression, the infix loop finds rules['-'].infix = binary and calls that instead. Same token,two behaviors,decided purely by position.

2. The precedence loop is what enforces operator grouping: each call to parsePrecedence(P) keeps consuming infix operators only while the next operator's precedence >= P. This single comparison makes'*' bind tighter than '+' and stops operands from being stolen by lower-precedence operators.

3. grouping() emits no bytecode — parentheses are purely syntactic. Their only effect is changing which parsePrecedence() call "owns" the tokens inside.
