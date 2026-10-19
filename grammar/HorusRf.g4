grammar HorusRf;

// This file is the single manually maintained concrete grammar for HorusRF.
// Generated lexer/parser code from this grammar drives the production frontend.

program: characterization EOF;

characterization
    : CHARACTERIZE IDENTIFIER LEFT_BRACE statement* RIGHT_BRACE
    ;

statement
    : referenceStatement
    | sweepStatement
    | measurementStatement
    | calibrationStatement
    | deriveStatement
    | assertionStatement
    ;

referenceStatement: REFERENCE POWER EQUAL signedQuantity;

sweepStatement
    : SWEEP sweepDimension signedQuantity RANGE signedQuantity STEP signedQuantity
    ;

sweepDimension: FREQUENCY | TEMPERATURE;

measurementStatement
    : MEASURE (POWER | expressionName EQUAL expressionName)
    ;

deriveStatement: DERIVE IDENTIFIER EQUAL expression;
assertionStatement: ASSERT expression;

calibrationStatement
    : DERIVE CALIBRATION IDENTIFIER LEFT_BRACE
      CORRECTION EQUAL expression
      OVER expressionName (COMMA expressionName)*
      RIGHT_BRACE
    ;

expression: comparisonExpression;

comparisonExpression
    : additiveExpression (comparisonOperator additiveExpression)?
    ;

comparisonOperator
    : LESS | LESS_EQUAL | GREATER | GREATER_EQUAL | EQUAL_EQUAL | NOT_EQUAL
    ;

additiveExpression
    : unaryExpression ((PLUS | MINUS) unaryExpression)*
    ;

unaryExpression: MINUS? primaryExpression;

primaryExpression
    : quantity
    | reference
    | call
    | expressionName
    | LEFT_PAREN expression RIGHT_PAREN
    ;

call: expressionName LEFT_PAREN expression RIGHT_PAREN;
reference: REFERENCE DOT expressionName;
expressionName: IDENTIFIER | POWER | FREQUENCY | TEMPERATURE;
signedQuantity: MINUS? quantity;
quantity: NUMBER unit;

unit
    : UNIT_HZ | UNIT_KHZ | UNIT_MHZ | UNIT_GHZ | UNIT_DBM | UNIT_DB
    | UNIT_K | UNIT_DEGC | UNIT_DELTA_K | UNIT_DELTA_C | UNIT_PERCENT
    ;

CHARACTERIZE: 'characterize';
REFERENCE: 'reference';
POWER: 'power';
SWEEP: 'sweep';
FREQUENCY: 'frequency';
TEMPERATURE: 'temperature';
STEP: 'step';
MEASURE: 'measure';
DERIVE: 'derive';
CALIBRATION: 'calibration';
CORRECTION: 'correction';
OVER: 'over';
ASSERT: 'assert';

UNIT_HZ: 'Hz';
UNIT_KHZ: 'kHz';
UNIT_MHZ: 'MHz';
UNIT_GHZ: 'GHz';
UNIT_DBM: 'dBm';
UNIT_DB: 'dB';
UNIT_K: 'K';
UNIT_DEGC: 'degC';
UNIT_DELTA_K: 'deltaK';
UNIT_DELTA_C: 'deltaC';
UNIT_PERCENT: '%';

LEFT_BRACE: '{';
RIGHT_BRACE: '}';
EQUAL_EQUAL: '==';
NOT_EQUAL: '!=';
LESS_EQUAL: '<=';
GREATER_EQUAL: '>=';
EQUAL: '=';
LESS: '<';
GREATER: '>';
RANGE: '..';
DOT: '.';
PLUS: '+';
MINUS: '-';
LEFT_PAREN: '(';
RIGHT_PAREN: ')';
COMMA: ',';

// These alternatives deliberately form one invalid token, matching the
// the established treatment of malformed numeric runs. The predicate keeps
// the first dot in a range out of the trailing-decimal alternative.
MALFORMED_NUMBER
    : DIGIT+ '.' {(static_cast<void>(_localctx), _input->LA(1) != '.')}?
      DIGIT*
      ( (ASCII_LETTER | '_') (ASCII_LETTER | DIGIT | '_' | '.')*
      | '.' {(static_cast<void>(_localctx), _input->LA(1) != '.')}?
        (ASCII_LETTER | DIGIT | '_' | '.')*
      )
    | DIGIT+ (ASCII_LETTER | '_') (ASCII_LETTER | DIGIT | '_' | '.')*
    | DIGIT+ '.' {(static_cast<void>(_localctx), _input->LA(1) != '.')}?
    | '.' DIGIT+
    ;

NUMBER: DIGIT+ ('.' DIGIT+)?;
IDENTIFIER: ASCII_LETTER (ASCII_LETTER | DIGIT | '_')*;
LINE_COMMENT: '//' ~[\n]* -> skip;
WHITESPACE: [ \t\r\n\u000B\u000C]+ -> skip;
INVALID: .;

fragment ASCII_LETTER: [A-Za-z];
fragment DIGIT: [0-9];
