// LinuxCNCParser.cpp - Extracted from LinuxCNC interp_read.cc
// For comparative testing against FluidNC parser
// Modified to compile standalone without LinuxCNC dependencies

#include "LinuxCNCParser.h"
#include <cmath>
#include <cstring>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <strings.h>

#define M_PIl M_PI

// Simplified error handling for testing
enum ErrorCode {
    OK = 0,
    ERR_BAD_FORMAT = -1,
    ERR_DIVIDE_BY_ZERO = -2,
    ERR_BAD_OPERATION = -3,
    ERR_NO_BRACKET = -4,
    ERR_UNCLOSED_EXPRESSION = -5,
    ERR_UNKNOWN_UNARY = -6,
};

// Operation codes
enum Operation {
    PLUS = 1,
    MINUS = 2,
    TIMES = 3,
    DIVIDED_BY = 4,
    MODULO = 5,
    POWER = 6,
    AND2 = 7,
    EXCLUSIVE_OR = 8,
    NON_EXCLUSIVE_OR = 9,
    RIGHT_BRACKET = 10,
    EQ = 11,
    NE = 12,
    LT = 13,
    LE = 14,
    GT = 15,
    GE = 16,
};

enum UnaryOp {
    ABS = 1,
    ACOS = 2,
    ASIN = 3,
    ATAN = 4,
    COS = 5,
    EXP = 6,
    FIX = 7,
    FUP = 8,
    LN = 9,
    ROUND = 10,
    SIN = 11,
    SQRT = 12,
    TAN = 13,
    EXISTS = 14,
};

#define MAX_STACK 7

static int precedence(int op) {
    switch (op) {
        case RIGHT_BRACKET:
            return 0;
        case PLUS:
        case MINUS:
        case NON_EXCLUSIVE_OR:
        case EXCLUSIVE_OR:
        case AND2:
            return 1;
        case LT:
        case LE:
        case GT:
        case GE:
        case EQ:
        case NE:
            return 2;
        case TIMES:
        case DIVIDED_BY:
        case MODULO:
            return 3;
        case POWER:
            return 4;
        default:
            return 0;
    }
}

static int execute_unary(double *value, int op) {
    switch (op) {
        case ABS:
            *value = fabs(*value);
            break;
        case ACOS:
            *value = acos(*value / 180.0 * M_PI) * 180.0 / M_PI;
            break;
        case ASIN:
            *value = asin(*value / 180.0 * M_PI) * 180.0 / M_PI;
            break;
        case ATAN:
            *value = atan(*value / 180.0 * M_PI) * 180.0 / M_PI;
            break;
        case COS:
            *value = cos(*value * M_PI / 180.0);
            break;
        case EXP:
            *value = exp(*value);
            break;
        case FIX:
            *value = floor(*value);
            break;
        case FUP:
            *value = ceil(*value);
            break;
        case LN:
            *value = log(*value);
            break;
        case ROUND:
            *value = round(*value);
            break;
        case SIN:
            *value = sin(*value * M_PI / 180.0);
            break;
        case SQRT:
            *value = sqrt(*value);
            break;
        case TAN:
            *value = tan(*value * M_PI / 180.0);
            break;
        default:
            return ERR_UNKNOWN_UNARY;
    }
    return OK;
}

static int execute_binary(double *lhs, int op, double rhs) {
    switch (op) {
        case PLUS:
            *lhs = *lhs + rhs;
            break;
        case MINUS:
            *lhs = *lhs - rhs;
            break;
        case TIMES:
            *lhs = *lhs * rhs;
            break;
        case DIVIDED_BY:
            if (rhs == 0.0)
                return ERR_DIVIDE_BY_ZERO;
            *lhs = *lhs / rhs;
            break;
        case MODULO:
            *lhs = fmod(*lhs, rhs);
            break;
        case POWER:
            *lhs = pow(*lhs, rhs);
            break;
        case AND2:
            *lhs = (*lhs != 0.0) && (rhs != 0.0) ? 1.0 : 0.0;
            break;
        case EXCLUSIVE_OR:
            *lhs = ((*lhs != 0.0) || (rhs != 0.0)) && !((*lhs != 0.0) && (rhs != 0.0)) ? 1.0 : 0.0;
            break;
        case NON_EXCLUSIVE_OR:
            *lhs = (*lhs != 0.0) || (rhs != 0.0) ? 1.0 : 0.0;
            break;
        case EQ:
            *lhs = fabs(*lhs - rhs) < 0.00001 ? 1.0 : 0.0;
            break;
        case NE:
            *lhs = fabs(*lhs - rhs) >= 0.00001 ? 1.0 : 0.0;
            break;
        case LT:
            *lhs = *lhs < rhs ? 1.0 : 0.0;
            break;
        case LE:
            *lhs = *lhs <= rhs ? 1.0 : 0.0;
            break;
        case GT:
            *lhs = *lhs > rhs ? 1.0 : 0.0;
            break;
        case GE:
            *lhs = *lhs >= rhs ? 1.0 : 0.0;
            break;
        default:
            return ERR_BAD_OPERATION;
    }
    return OK;
}

static int read_real_number(const char *line, int *counter, double *result) {
    const char *start = line + *counter;
    
    // Handle +/- sign
    int sign = 1;
    if (*start == '-') {
        sign = -1;
        start++;
    } else if (*start == '+') {
        start++;
    }
    
    // Parse integer part
    double value = 0.0;
    int has_digits = 0;
    while (isdigit(*start)) {
        value = value * 10.0 + (*start - '0');
        has_digits = 1;
        start++;
    }
    
    // Parse decimal part
    if (*start == '.' && isdigit(start[1])) {
        start++;
        double decimal_place = 0.1;
        while (isdigit(*start)) {
            value += decimal_place * (*start - '0');
            decimal_place *= 0.1;
            has_digits = 1;
            start++;
        }
    }
    
    if (!has_digits)
        return ERR_BAD_FORMAT;
    
    *result = sign * value;
    *counter = start - line;
    return OK;
}

static int read_operation(const char *line, int *counter, int *operation) {
    char c = line[*counter];
    
    if (c == 0)
        return ERR_UNCLOSED_EXPRESSION;
    
    *counter = *counter + 1;
    
    switch (tolower(c)) {
        case '+':
            *operation = PLUS;
            break;
        case '-':
            *operation = MINUS;
            break;
        case '*':
            if (line[*counter] == '*') {
                *operation = POWER;
                *counter = *counter + 1;
            } else {
                *operation = TIMES;
            }
            break;
        case '/':
            *operation = DIVIDED_BY;
            break;
        case ']':
            *operation = RIGHT_BRACKET;
            break;
        case 'a':
            if (strncasecmp(line + *counter, "nd", 2) == 0) {
                *operation = AND2;
                *counter += 2;
            } else {
                return ERR_BAD_OPERATION;
            }
            break;
        case 'm':
            if (strncasecmp(line + *counter, "od", 2) == 0) {
                *operation = MODULO;
                *counter += 2;
            } else {
                return ERR_BAD_OPERATION;
            }
            break;
        case 'o':
            if (tolower(line[*counter]) == 'r') {
                *operation = NON_EXCLUSIVE_OR;
                *counter += 1;
            } else {
                return ERR_BAD_OPERATION;
            }
            break;
        case 'x':
            if (strncasecmp(line + *counter, "or", 2) == 0) {
                *operation = EXCLUSIVE_OR;
                *counter += 2;
            } else {
                return ERR_BAD_OPERATION;
            }
            break;
        case 'e':
            if (tolower(line[*counter]) == 'q') {
                *operation = EQ;
                *counter += 1;
            } else {
                return ERR_BAD_OPERATION;
            }
            break;
        case 'n':
            if (tolower(line[*counter]) == 'e') {
                *operation = NE;
                *counter += 1;
            } else {
                return ERR_BAD_OPERATION;
            }
            break;
        case 'l':
            if (tolower(line[*counter]) == 't') {
                *operation = LT;
                *counter += 1;
            } else if (tolower(line[*counter]) == 'e') {
                *operation = LE;
                *counter += 1;
            } else {
                return ERR_BAD_OPERATION;
            }
            break;
        case 'g':
            if (tolower(line[*counter]) == 't') {
                *operation = GT;
                *counter += 1;
            } else if (tolower(line[*counter]) == 'e') {
                *operation = GE;
                *counter += 1;
            } else {
                return ERR_BAD_OPERATION;
            }
            break;
        default:
            return ERR_BAD_OPERATION;
    }
    return OK;
}

static int read_operation_unary(const char *line, int *counter, int *operation) {
    char c = line[*counter];
    *counter = *counter + 1;
    
    // Convert to lowercase for comparison
    c = tolower(c);
    
    switch (c) {
        case 'a':
            if (strncasecmp(line + *counter, "bs", 2) == 0) {
                *operation = ABS;
                *counter += 2;
            } else if (strncasecmp(line + *counter, "cos", 3) == 0) {
                *operation = ACOS;
                *counter += 3;
            } else if (strncasecmp(line + *counter, "sin", 3) == 0) {
                *operation = ASIN;
                *counter += 3;
            } else if (strncasecmp(line + *counter, "tan", 3) == 0) {
                *operation = ATAN;
                *counter += 3;
            } else {
                return ERR_UNKNOWN_UNARY;
            }
            break;
        case 'c':
            if (strncasecmp(line + *counter, "os", 2) == 0) {
                *operation = COS;
                *counter += 2;
            } else {
                return ERR_UNKNOWN_UNARY;
            }
            break;
        case 'e':
            if (strncasecmp(line + *counter, "xp", 2) == 0) {
                *operation = EXP;
                *counter += 2;
            } else if (strncasecmp(line + *counter, "xists", 5) == 0) {
                *operation = EXISTS;
                *counter += 5;
            } else {
                return ERR_UNKNOWN_UNARY;
            }
            break;
        case 'f':
            if (strncasecmp(line + *counter, "ix", 2) == 0) {
                *operation = FIX;
                *counter += 2;
            } else if (strncasecmp(line + *counter, "up", 2) == 0) {
                *operation = FUP;
                *counter += 2;
            } else {
                return ERR_UNKNOWN_UNARY;
            }
            break;
        case 'l':
            if (tolower(line[*counter]) == 'n') {
                *operation = LN;
                *counter += 1;
            } else {
                return ERR_UNKNOWN_UNARY;
            }
            break;
        case 'r':
            if (strncasecmp(line + *counter, "ound", 4) == 0) {
                *operation = ROUND;
                *counter += 4;
            } else {
                return ERR_UNKNOWN_UNARY;
            }
            break;
        case 's':
            if (strncasecmp(line + *counter, "in", 2) == 0) {
                *operation = SIN;
                *counter += 2;
            } else if (strncasecmp(line + *counter, "qrt", 3) == 0) {
                *operation = SQRT;
                *counter += 3;
            } else {
                return ERR_UNKNOWN_UNARY;
            }
            break;
        case 't':
            if (strncasecmp(line + *counter, "an", 2) == 0) {
                *operation = TAN;
                *counter += 2;
            } else {
                return ERR_UNKNOWN_UNARY;
            }
            break;
        default:
            return ERR_UNKNOWN_UNARY;
    }
    return OK;
}

static int read_real_expression(const char *line, int *counter, double *value);

static int read_unary(const char *line, int *counter, double *value) {
    int operation;
    int status = read_operation_unary(line, counter, &operation);
    if (status != OK)
        return status;
    
    if (line[*counter] != '[')
        return ERR_NO_BRACKET;
    
    if (operation == EXISTS) {
        // Simplified for testing
        *value = 1.0;
        (*counter)++;
        while (line[*counter] && line[*counter] != ']')
            (*counter)++;
        if (line[*counter] == ']')
            (*counter)++;
        return OK;
    }
    
    status = read_real_expression(line, counter, value);
    if (status != OK)
        return status;
    
    if (operation == ATAN) {
        // atan[x]/[y] format - simplified
        if (line[*counter] == '/') {
            (*counter)++;
            double arg2;
            status = read_real_expression(line, counter, &arg2);
            if (status != OK)
                return status;
            *value = atan2(*value, arg2) * 180.0 / M_PI;
        }
    } else {
        status = execute_unary(value, operation);
    }
    
    return status;
}

static int read_real_expression(const char *line, int *counter, double *value) {
    double values[MAX_STACK];
    int operators[MAX_STACK];
    int stack_index;
    int status;
    
    if (line[*counter] != '[')
        return ERR_NO_BRACKET;
    
    (*counter)++;
    
    // Read first value
    char c = line[*counter];
    if (c == '[') {
        status = read_real_expression(line, counter, &values[0]);
    } else if (c == '#') {
        // Parameter - simplified to 0 for testing
        values[0] = 0.0;
        (*counter)++;
        while (line[*counter] && !isdigit(line[*counter]) && line[*counter] != ']')
            (*counter)++;
        while (line[*counter] && isdigit(line[*counter]))
            (*counter)++;
    } else if ((c == '-' || c == '+') && line[*counter + 1] && 
               !isdigit(line[*counter + 1]) && line[*counter + 1] != '.') {
        // Unary operator
        (*counter)++;
        status = read_real_expression(line, counter, &values[0]);
        if (c == '-')
            values[0] = -values[0];
    } else if (isalpha(c)) {
        // Unary function
        status = read_unary(line, counter, &values[0]);
    } else {
        status = read_real_number(line, counter, &values[0]);
    }
    
    if (status != OK)
        return status;
    
    status = read_operation(line, counter, &operators[0]);
    if (status != OK)
        return status;
    
    stack_index = 1;
    
    while (operators[0] != RIGHT_BRACKET) {
        // Read next value
        c = line[*counter];
        if (c == '[') {
            status = read_real_expression(line, counter, &values[stack_index]);
        } else if (isalpha(c)) {
            status = read_unary(line, counter, &values[stack_index]);
        } else {
            status = read_real_number(line, counter, &values[stack_index]);
        }
        
        if (status != OK)
            return status;
        
        status = read_operation(line, counter, &operators[stack_index]);
        if (status != OK)
            return status;
        
        if (precedence(operators[stack_index]) > precedence(operators[stack_index - 1])) {
            stack_index++;
        } else {
            while (precedence(operators[stack_index]) <= precedence(operators[stack_index - 1])) {
                status = execute_binary(&values[stack_index - 1], operators[stack_index - 1], values[stack_index]);
                if (status != OK)
                    return status;
                
                operators[stack_index - 1] = operators[stack_index];
                
                if ((stack_index > 1) && 
                    (precedence(operators[stack_index - 1]) <= precedence(operators[stack_index - 2]))) {
                    stack_index--;
                } else {
                    break;
                }
            }
        }
    }
    
    *value = values[0];
    return OK;
}

// Main entry point - matches FluidNC's read_number() signature
int linuxcnc_read_value(const char *line, int *counter, double *result) {
    char c = line[*counter];
    char c1 = line[*counter + 1];
    
    if (c == '[') {
        return read_real_expression(line, counter, result);
    } else if (c == '-' && c1 && !isdigit(c1) && c1 != '.') {
        (*counter)++;
        int status = linuxcnc_read_value(line, counter, result);
        if (status == OK)
            *result = -*result;
        return status;
    } else if (c == '+' && c1 && !isdigit(c1) && c1 != '.') {
        (*counter)++;
        return linuxcnc_read_value(line, counter, result);
    } else if (isalpha(c)) {
        return read_unary(line, counter, result);
    } else {
        return read_real_number(line, counter, result);
    }
}
