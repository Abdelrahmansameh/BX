grammar BX;

program: (globalVar | proc | func)*;

globalVar:
        'var' globalVarInit (',' globalVarInit)* ':' type ';';
globalVarInit: ID '=' (NUM | BOOL);

proc: 'proc' ID '(' (param (',' param)*)? ')' block;
func: 'fun' ID '(' (param (',' param)*)? ')' ':' type block;

type: 'int64' | 'bool';

param: ID (',' ID)* ':' type;

stmt: ID '=' expr ';'            # assign
    | expr ';'                   # eval
    | varDecl                    # declare
    | 'print' expr ';'           # print
    | block                      # scope
    | ifElse                     # if
    | 'while' '(' expr ')' block # while
    | 'return' expr? ';'         # return;

varDecl: 'var' varInit (',' varInit)* ':' type ';';
varInit: ID ('=' expr);

ifElse: 'if' '(' expr ')' block ('else' (ifElse | block))?;

block: '{' stmt* '}';

expr: ID                                       # variable
    | ID '(' (expr (',' expr)*)? ')'           # call
    | NUM                                      # number
    | BOOL                                     # bool
    | op = ('~' | '-' | '!') expr              # unop
    | expr op = ('*' | '/' | '%') expr         # multiplicative
    | expr op = ('+' | '-') expr               # additive
    | expr op = ('<<' | '>>') expr             # shift
    | expr op = ('<' | '<=' | '>' | '>=') expr # inequation
    | expr op = ('==' | '!=') expr             # equation
    | expr '&' expr                            # bitAnd
    | expr '^' expr                            # bitXor
    | expr '|' expr                            # bitOr
    | expr '&&' expr                           # logAnd
    | expr '||' expr                           # logOr
    | '(' expr ')'                             # parens;

BOOL: 'true' | 'false';
ID: [A-Za-z_][A-Za-z0-9_]*;
NUM: '-'? [0-9]+;

LINECOMMENT: '//' ~[\r\n]* '\r'? ('\n' | EOF) -> skip;
WS: [ \t\r\n]+ -> skip;