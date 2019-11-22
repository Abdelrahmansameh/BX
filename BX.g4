grammar BX;

program: (globalVar | proc | func | type_abbrev)*;

type_abbrev: 'type' ID '=' type ';';

type: 'int64'           #inttype
    | 'bool'            #booltype
    | type '*'          #pointertype
    | type '[' NUM ']'  #listtype
    | struct_type       #structtype;
struct_type: 'struct' '{' (struct_field (',' struct_field)*','?)? '}';
struct_field: ID ':' type;

globalVar:
        'var' globalVarInit (',' globalVarInit)* ':' type ';';
globalVarInit: ID '=' (NUM | BOOL);

func: 'fun' ID '(' parameter_groups? ')' ':' type block; 
proc: 'proc' ID '(' parameter_groups? ')' block;

parameter_groups: param (',' param)?;
param: ID (',' ID)* ':' type;

stmt: expr '=' expr ';'          # assign
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

expr: ID '(' (expr (',' expr)*)? ')'                  # call
    | ('alloc' type '[' expr ']' | 'null' | '&' expr) # alloc
    | ID                                              # ID
    | '*'expr                                         # deref
    | expr '[' expr ']'                               # list
    | NUM                                             # number
    | BOOL                                            # bool
    | op = ('~' | '-' | '!') expr                     # unop
    | expr op = ('*' | '/' | '%') expr                # multiplicative
    | expr op = ('+' | '-') expr                      # additive
    | expr op = ('<<' | '>>') expr                    # shift
    | expr op = ('<' | '<=' | '>' | '>=') expr        # inequation
    | expr op = ('==' | '!=') expr                    # equation
    | expr '&' expr                                   # bitAnd
    | expr '^' expr                                   # bitXor
    | expr '|' expr                                   # bitOr
    | expr '&&' expr                                  # logAnd
    | expr '||' expr                                  # logOr
    | '(' expr ')'                                    # parens;


BOOL: 'true' | 'false';
ID: [A-Za-z_][A-Za-z0-9_]*;
NUM: '-'? [0-9]+;

LINECOMMENT: '//' ~[\r\n]* '\r'? ('\n' | EOF) -> skip;
WS: [ \t\r\n]+ -> skip;
