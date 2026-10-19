grammar HorusRfAntlrSmoke;

document: WORD EOF;

WORD: [a-z]+;
WHITESPACE: [ \t\r\n]+ -> skip;
