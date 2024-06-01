// ngc_expr.c - derived from:

/********************************************************************
* Description: interp_execute.cc
*
*   Derived from a work by Thomas Kramer
*
* Author:
* License: GPL Version 2
* System: Linux
*
* Copyright (c) 2004 All rights reserved.
*
* Last change:
********************************************************************/

/* Modified by Terje Io for grblHAL */
// Further modified by Mitch Bradley for FluidNC

#include "Config.h"
#include "NutsBolts.h"
#include "Parameters.h"
#include "Logging.h"

#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define DEGRAD (180 / M_PI)
#define RADDEG (M_PI / 180)
#define TOLERANCE_EQUAL 0.00001

#include "Error.h"

#include "Expression.h"

#define MAX_STACK 7

typedef enum {
    NGCBinaryOp_NoOp = 0,
    NGCBinaryOp_DividedBy,
    NGCBinaryOp_Modulo,
    NGCBinaryOp_Power,
    NGCBinaryOp_Times,
    NGCBinaryOp_Binary2 = NGCBinaryOp_Times,
    NGCBinaryOp_And2,
    NGCBinaryOp_ExclusiveOR,
    NGCBinaryOp_Minus,
    NGCBinaryOp_NotExclusiveOR,
    NGCBinaryOp_Plus,
    NGCBinaryOp_RightBracket,
    NGCBinaryOp_RelationalFirst,
    NGCBinaryOp_LT = NGCBinaryOp_RelationalFirst,
    NGCBinaryOp_EQ,
    NGCBinaryOp_NE,
    NGCBinaryOp_LE,
    NGCBinaryOp_GE,
    NGCBinaryOp_GT,
    NGCBinaryOp_RelationalLast = NGCBinaryOp_GT,
} ngc_binary_op_t;

typedef enum {
    NGCUnaryOp_ABS = 1,
    NGCUnaryOp_ACOS,
    NGCUnaryOp_ASIN,
    NGCUnaryOp_ATAN,
    NGCUnaryOp_COS,
    NGCUnaryOp_EXP,
    NGCUnaryOp_FIX,
    NGCUnaryOp_FUP,
    NGCUnaryOp_LN,
    NGCUnaryOp_Round,
    NGCUnaryOp_SIN,
    NGCUnaryOp_SQRT,
    NGCUnaryOp_TAN,
    NGCUnaryOp_Exists,  // Not implemented
} ngc_unary_op_t;

/*! \brief Executes the operations: /, MOD, ** (POW), *.

\param lhs pointer to the left hand side operand and result.
\param operation \ref ngc_binary_op_t enum value.
\param rhs pointer to the right hand side operand.
\returns #Error::Ok enum value if processed without error, appropriate \ref Error enum value if not.
*/
static Error execute_binary1(float& lhs, ngc_binary_op_t operation, float& rhs) {
    Error status = Error::Ok;

    switch (operation) {
        case NGCBinaryOp_DividedBy:
            if (rhs == 0.0f || rhs == -0.0f)
                status = Error::ExpressionDivideByZero;  // Attempt to divide by zero
            else
                lhs = lhs / rhs;
            break;

        case NGCBinaryOp_Modulo:  // always calculates a positive answer
            lhs = fmodf(lhs, rhs);
            if (lhs < 0.0f)
                lhs = lhs + fabsf(rhs);
            break;

        case NGCBinaryOp_Power:
            if (lhs < 0.0f && floorf(rhs) != rhs)
                status = Error::ExpressionInvalidArgument;  // Attempt to raise negative value to non-integer power
            else
                lhs = powf(lhs, rhs);
            break;

        case NGCBinaryOp_Times:
            lhs = lhs * rhs;
            break;

        default:
            status = Error::ExpressionUnknownOp;
    }

    return status;
}

/*! \brief Executes the operations: +, -, AND, OR, XOR, EQ, NE, LT, LE, GT, GE
The RS274/NGC manual does not say what
the calculated value of the logical operations should be. This
function calculates either 1.0 (meaning true) or 0.0 (meaning false).
Any non-zero input value is taken as meaning true, and only 0.0 means false.

\param lhs pointer to the left hand side operand and result.
\param operation \ref ngc_binary_op_t enum value.
\param rhs pointer to the right hand side operand.
\returns #Error::Ok enum value if processed without error, appropriate \ref Error enum value if not.
*/
static Error execute_binary2(float& lhs, ngc_binary_op_t operation, float& rhs) {
    switch (operation) {
        case NGCBinaryOp_And2:
            lhs = ((lhs == 0.0f) || (rhs == 0.0f)) ? 0.0f : 1.0f;
            break;

        case NGCBinaryOp_ExclusiveOR:
            lhs = (((lhs == 0.0f) && (rhs != 0.0f)) || ((lhs != 0.0f) && (rhs == 0.0f))) ? 1.0f : 0.0f;
            break;

        case NGCBinaryOp_Minus:
            lhs = (lhs - rhs);
            break;

        case NGCBinaryOp_NotExclusiveOR:
            lhs = ((lhs != 0.0f) || (rhs != 0.0f)) ? 1.0f : 0.0f;
            break;

        case NGCBinaryOp_Plus:
            lhs = (lhs + rhs);
            break;

        case NGCBinaryOp_LT:
            lhs = (lhs < rhs) ? 1.0f : 0.0f;
            break;

        case NGCBinaryOp_EQ: {
            float diff = lhs - rhs;
            diff       = (diff < 0.0f) ? -diff : diff;
            lhs        = (diff < TOLERANCE_EQUAL) ? 1.0f : 0.0f;
        } break;

        case NGCBinaryOp_NE: {
            float diff = lhs - rhs;
            diff       = (diff < 0.0f) ? -diff : diff;
            lhs        = (diff >= TOLERANCE_EQUAL) ? 1.0f : 0.0f;
        } break;

        case NGCBinaryOp_LE:
            lhs = (lhs <= rhs) ? 1.0f : 0.0f;
            break;

        case NGCBinaryOp_GE:
            lhs = (lhs >= rhs) ? 1.0f : 0.0f;
            break;

        case NGCBinaryOp_GT:
            lhs = (lhs > rhs) ? 1.0f : 0.0f;
            break;

        default:
            return Error::ExpressionUnknownOp;
    }

    return Error::Ok;
}

/*! \brief Executes a binary operation.

This just calls either execute_binary1 or execute_binary2.

\param lhs pointer to the left hand side operand and result.
\param operation \ref ngc_binary_op_t enum value.
\param rhs pointer to the right hand side operand.
\returns #Error::Ok enum value if processed without error, appropriate \ref Error enum value if not.
*/
static Error execute_binary(float& lhs, ngc_binary_op_t operation, float& rhs) {
    if (operation <= NGCBinaryOp_Binary2)
        return execute_binary1(lhs, operation, rhs);

    return execute_binary2(lhs, operation, rhs);
}

/*! \brief Executes an unary operation: ABS, ACOS, ASIN, COS, EXP, FIX, FUP, LN, ROUND, SIN, SQRT, TAN

All angle measures in the input or output are in degrees.

\param operand pointer to the operand.
\param operation \ref ngc_binary_op_t enum value.
\returns #Error::Ok enum value if processed without error, appropriate \ref Error enum value if not.
*/
static Error execute_unary(float& operand, ngc_unary_op_t operation) {
    Error status = Error::Ok;

    switch (operation) {
        case NGCUnaryOp_ABS:
            if (operand < 0.0f)
                operand = (-1.0f * operand);
            break;

        case NGCUnaryOp_ACOS:
            if (operand < -1.0f || operand > 1.0f)
                status = Error::ExpressionArgumentOutOfRange;  // Argument to ACOS out of range
            else
                operand = acosf(operand) * DEGRAD;
            break;

        case NGCUnaryOp_ASIN:
            if (operand < -1.0f || operand > 1.0f)
                status = Error::ExpressionArgumentOutOfRange;  // Argument to ASIN out of range
            else
                operand = asinf(operand) * DEGRAD;
            break;

        case NGCUnaryOp_COS:
            operand = cosf(operand * RADDEG);
            break;

        case NGCUnaryOp_Exists:
            // do nothing here, result for the EXISTS function is set by read_unary()
            break;

        case NGCUnaryOp_EXP:
            operand = expf(operand);
            break;

        case NGCUnaryOp_FIX:
            operand = floorf(operand);
            break;

        case NGCUnaryOp_FUP:
            operand = ceilf(operand);
            break;

        case NGCUnaryOp_LN:
            if (operand <= 0.0f)
                status = Error::ExpressionArgumentOutOfRange;  // Argument to LN out of range
            else
                operand = logf(operand);
            break;

        case NGCUnaryOp_Round:
            operand = (float)((int)(operand + ((operand < 0.0f) ? -0.5f : 0.5f)));
            break;

        case NGCUnaryOp_SIN:
            operand = sinf(operand * RADDEG);
            break;

        case NGCUnaryOp_SQRT:
            if (operand < 0.0f)
                status = Error::ExpressionArgumentOutOfRange;  // Negative argument to SQRT
            else
                operand = sqrtf(operand);
            break;

        case NGCUnaryOp_TAN:
            operand = tanf(operand * RADDEG);
            break;

        default:
            status = Error::ExpressionUnknownOp;
    }

    return status;
}

/*! \brief Returns an integer representing the precedence level of an operator.

\param op \ref ngc_binary_op_t enum value.
\returns precedence level.
*/
static uint_fast8_t precedence(ngc_binary_op_t op) {
    switch (op) {
        case NGCBinaryOp_RightBracket:
            return 1;

        case NGCBinaryOp_And2:
        case NGCBinaryOp_ExclusiveOR:
        case NGCBinaryOp_NotExclusiveOR:
            return 2;

        case NGCBinaryOp_LT:
        case NGCBinaryOp_EQ:
        case NGCBinaryOp_NE:
        case NGCBinaryOp_LE:
        case NGCBinaryOp_GE:
        case NGCBinaryOp_GT:
            return 3;

        case NGCBinaryOp_Minus:
        case NGCBinaryOp_Plus:
            return 4;

        case NGCBinaryOp_NoOp:
        case NGCBinaryOp_DividedBy:
        case NGCBinaryOp_Modulo:
        case NGCBinaryOp_Times:
            return 5;

        case NGCBinaryOp_Power:
            return 6;

        default:
            break;
    }

    return 0;  // should never happen
}

/*! \brief Reads a binary operation out of the line
starting at the index given by the pos offset. If a valid one is found, the
value of operation is set to the symbolic value for that operation.

\param line pointer to RS274/NGC code (block).
\param pos offset into line where expression starts.
\param operation pointer to \ref ngc_binary_op_t enum value.
\returns #Error::Ok enum value if processed without error, appropriate \ref Error enum value if not.
*/
static Error read_operation(const char* line, size_t* pos, ngc_binary_op_t* operation) {
    char  c      = line[*pos];
    Error status = Error::Ok;

    (*pos)++;

    switch (c) {
        case '+':
            *operation = NGCBinaryOp_Plus;
            break;

        case '-':
            *operation = NGCBinaryOp_Minus;
            break;

        case '/':
            *operation = NGCBinaryOp_DividedBy;
            break;

        case '*':
            if (line[*pos] == '*') {
                *operation = NGCBinaryOp_Power;
                (*pos)++;
            } else
                *operation = NGCBinaryOp_Times;
            break;

        case ']':
            *operation = NGCBinaryOp_RightBracket;
            break;

        case 'A':
            if (!strncmp(line + *pos, "ND", 2)) {
                *operation = NGCBinaryOp_And2;
                *pos += 2;
            } else
                status = Error::ExpressionUnknownOp;  // Unknown operation name starting with A
            break;

        case 'M':
            if (!strncmp(line + *pos, "OD", 2)) {
                *operation = NGCBinaryOp_Modulo;
                *pos += 2;
            } else
                status = Error::ExpressionUnknownOp;  // Unknown operation name starting with M
            break;

        case 'R':
            if (line[*pos] == 'R') {
                *operation = NGCBinaryOp_NotExclusiveOR;
                (*pos)++;
            } else
                status = Error::ExpressionUnknownOp;  // Unknown operation name starting with R
            break;

        case 'X':
            if (!strncmp(line + *pos, "OR", 2)) {
                *operation = NGCBinaryOp_ExclusiveOR;
                *pos += 2;
            } else
                status = Error::ExpressionUnknownOp;  // Unknown operation name starting with X
            break;

        /* relational operators */
        case 'E':
            if (line[*pos] == 'Q') {
                *operation = NGCBinaryOp_EQ;
                (*pos)++;
            } else
                status = Error::ExpressionUnknownOp;  // Unknown operation name starting with E
            break;

        case 'N':
            if (line[*pos] == 'E') {
                *operation = NGCBinaryOp_NE;
                (*pos)++;
            } else
                status = Error::ExpressionUnknownOp;  // Unknown operation name starting with N
            break;

        case 'G':
            if (line[*pos] == 'E') {
                *operation = NGCBinaryOp_GE;
                (*pos)++;
            } else if (line[*pos] == 'T') {
                *operation = NGCBinaryOp_GT;
                (*pos)++;
            } else
                status = Error::ExpressionUnknownOp;  // Unknown operation name starting with G
            break;

        case 'L':
            if (line[*pos] == 'E') {
                *operation = NGCBinaryOp_LE;
                (*pos)++;
            } else if (line[*pos] == 'T') {
                *operation = NGCBinaryOp_LT;
                (*pos)++;
            } else
                status = Error::ExpressionUnknownOp;  // Unknown operation name starting with L
            break;

            //        case '\0':
            //            status = Error::ExpressionUnknownOp; // No operation name found

        default:
            status = Error::ExpressionUnknownOp;  // Unknown operation name
    }

    return status;
}

/*! \brief Reads the name of an unary operation out of the line
starting at the index given by the pos offset. If a valid one is found, the
value of operation is set to the symbolic value for that operation.

\param line pointer to RS274/NGC code (block).
\param pos offset into line where expression starts.
\param operation pointer to \ref ngc_unary_op_t enum value.
\returns #Error::Ok enum value if processed without error, appropriate \ref Error enum value if not.
*/
static Error read_operation_unary(const char* line, size_t* pos, ngc_unary_op_t* operation) {
    char  c      = line[*pos];
    Error status = Error::Ok;

    (*pos)++;

    switch (c) {
        case 'A':
            if (!strncmp(line + *pos, "BS", 2)) {
                *operation = NGCUnaryOp_ABS;
                *pos += 2;
            } else if (!strncmp(line + *pos, "COS", 3)) {
                *operation = NGCUnaryOp_ACOS;
                *pos += 3;
            } else if (!strncmp(line + *pos, "SIN", 3)) {
                *operation = NGCUnaryOp_ASIN;
                *pos += 3;
            } else if (!strncmp(line + *pos, "TAN", 3)) {
                *operation = NGCUnaryOp_ATAN;
                *pos += 3;
            } else
                status = Error::ExpressionUnknownOp;
            break;

        case 'C':
            if (!strncmp(line + *pos, "OS", 2)) {
                *operation = NGCUnaryOp_COS;
                *pos += 2;
            } else
                status = Error::ExpressionUnknownOp;
            break;

        case 'E':
            if (!strncmp(line + *pos, "XP", 2)) {
                *operation = NGCUnaryOp_EXP;
                *pos += 2;
            } else if (!strncmp(line + *pos, "XISTS", 5)) {
                *operation = NGCUnaryOp_Exists;
                *pos += 5;
            } else
                status = Error::ExpressionUnknownOp;
            break;

        case 'F':
            if (!strncmp(line + *pos, "IX", 2)) {
                *operation = NGCUnaryOp_FIX;
                *pos += 2;
            } else if (!strncmp(line + *pos, "UP", 2)) {
                *operation = NGCUnaryOp_FUP;
                *pos += 2;
            } else
                status = Error::ExpressionUnknownOp;
            break;

        case 'L':
            if (line[*pos] == 'N') {
                *operation = NGCUnaryOp_LN;
                (*pos)++;
            } else
                status = Error::ExpressionUnknownOp;
            break;

        case 'R':
            if (!strncmp(line + *pos, "OUND", 4)) {
                *operation = NGCUnaryOp_Round;
                *pos += 4;
            } else
                status = Error::ExpressionUnknownOp;
            break;

        case 'S':
            if (!strncmp(line + *pos, "IN", 2)) {
                *operation = NGCUnaryOp_SIN;
                *pos += 2;
            } else if (!strncmp((line + *pos), "QRT", 3)) {
                *operation = NGCUnaryOp_SQRT;
                *pos += 3;
            } else
                status = Error::ExpressionUnknownOp;
            break;

        case 'T':
            if (!strncmp(line + *pos, "AN", 2)) {
                *operation = NGCUnaryOp_TAN;
                *pos += 2;
            } else
                status = Error::ExpressionUnknownOp;
            break;

        default:
            status = Error::ExpressionUnknownOp;
    }

    return status;
}

/*! \brief Reads a slash and the second argument to the ATAN function,
starting at the index given by the pos offset. Then it computes the value
of the ATAN operation applied to the two arguments.

\param line pointer to RS274/NGC code (block).
\param pos offset into line where expression starts.
\param value pointer to float where result is to be stored.
\returns #Error::Ok enum value if processed without error, appropriate \ref Error enum value if not.
*/
static Error read_atan(const char* line, size_t* pos, float& value) {
    float argument2;

    if (line[*pos] != '/')
        return Error::ExpressionSyntaxError;  // Slash missing after first ATAN argument

    (*pos)++;

    if (line[*pos] != '[')
        return Error::ExpressionSyntaxError;  // Left bracket missing after slash with ATAN;

    Error status;

    if ((status = expression(line, pos, argument2)) == Error::Ok)
        value = atan2f(value, argument2) * DEGRAD; /* value in radians, convert to degrees */

    return status;
}

/*! \brief Reads the value out of an unary operation of the line, starting at the
index given by the pos offset. The ATAN operation is
handled specially because it is followed by two arguments.

\param line pointer to RS274/NGC code (block).
\param pos offset into line where expression starts.
\param value pointer to float where result is to be stored.
\returns #Error::Ok enum value if processed without error, appropriate \ref Error enum value if not.
*/
Error read_unary(const char* line, size_t* pos, float& value) {
    ngc_unary_op_t operation;
    Error          status;

    if ((status = read_operation_unary(line, pos, &operation)) != Error::Ok) {
        return status;
    }
    if (line[*pos] != '[') {
        return Error::ExpressionSyntaxError;  // Left bracket missing after unary operation name
    }
    if (operation == NGCUnaryOp_Exists) {
        ++*pos;
        std::string arg;
        char        c;
        while ((c = line[*pos]) && c != ']') {
            ++*pos;
            arg += c;
        }
        if (!c) {
            return Error::ExpressionSyntaxError;
        }
        ++*pos;
        value = named_param_exists(arg);
        return Error::Ok;
    }
    if ((status = expression(line, pos, value)) != Error::Ok) {
        return status;
    }
    if (operation == NGCUnaryOp_ATAN) {
        return read_atan(line, pos, value);
    }
    return execute_unary(value, operation);
}

/*! \brief Evaluate expression and set result if successful.

\param line pointer to RS274/NGC code (block).
\param pos offset into line where expression starts.
\param value pointer to float where result is to be stored.
\returns #Error::Ok enum value if evaluated without error, appropriate \ref Error enum value if not.
*/
Error expression(const char* line, size_t* pos, float& value) {
    float           values[MAX_STACK];
    ngc_binary_op_t operators[MAX_STACK];
    uint_fast8_t    stack_index = 1;

    if (line[*pos] != '[')
        return Error::GcodeUnsupportedCommand;

    (*pos)++;

    Error status;

    if ((!read_number(line, pos, values[0], true)))
        return Error::BadNumberFormat;

    if ((status = read_operation(line, pos, operators)) != Error::Ok)
        return status;

    for (; operators[0] != NGCBinaryOp_RightBracket;) {
        if ((!read_number(line, pos, values[stack_index], true)))
            return Error::BadNumberFormat;

        if ((status = read_operation(line, pos, operators + stack_index)) != Error::Ok)
            return status;

        if (precedence(operators[stack_index]) > precedence(operators[stack_index - 1]))
            stack_index++;
        else {  // precedence of latest operator is <= previous precedence
            for (; precedence(operators[stack_index]) <= precedence(operators[stack_index - 1]);) {
                if ((status = execute_binary(values[stack_index - 1], operators[stack_index - 1], values[stack_index])) != Error::Ok)
                    return status;

                operators[stack_index - 1] = operators[stack_index];
                // auto o1 = operators[stack_index - 1];
                // auto o2 = operators[stack_index - 2];
                // auto p1 = precedence(o1);
                // auto p2 = precedence(o1);
                // if ((stack_index > 1) && p1 <= p2)
                if ((stack_index > 1) && precedence(operators[stack_index - 1]) <= precedence(operators[stack_index - 2]))
                    stack_index--;
                else
                    break;
            }
        }
    }

    value = values[0];

    return Error::Ok;
}
