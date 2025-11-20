// Expression.cpp - derived from
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
#define _USE_MATH_DEFINES
#include <cmath>
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
    Binary_NoOp = 0,
    Binary_DividedBy,
    Binary_Modulo,
    Binary_Power,
    Binary_Times,
    Binary_Binary2 = Binary_Times,
    Binary_And2,
    Binary_ExclusiveOR,
    Binary_Minus,
    Binary_NotExclusiveOR,
    Binary_Plus,
    Binary_RightBracket,
    Binary_RelationalFirst,
    Binary_LT = Binary_RelationalFirst,
    Binary_EQ,
    Binary_NE,
    Binary_LE,
    Binary_GE,
    Binary_GT,
    Binary_RelationalLast = Binary_GT,
} ngc_binary_op_t;

typedef enum {
    Unary_ABS = 1,
    Unary_ACOS,
    Unary_ASIN,
    Unary_ATAN,
    Unary_COS,
    Unary_EXP,
    Unary_FIX,
    Unary_FUP,
    Unary_LN,
    Unary_Round,
    Unary_SIN,
    Unary_SQRT,
    Unary_TAN,
    Unary_Exists,  // Not implemented
} ngc_unary_op_t;

static void report_param_error(Error err) {
    switch (err) {
        case Error::ExpressionDivideByZero:
            log_error("Divide by zero");
            break;
        case Error::ExpressionArgumentOutOfRange:
            log_error("Argument out of range");
            break;
        default:
            break;
    }
}

/*! \brief Executes the operations: /, MOD, ** (POW), *.

\param lhs pointer to the left hand side operand and result.
\param operation \ref ngc_binary_op_t enum value.
\param rhs pointer to the right hand side operand.
\returns #Error::Ok enum value if processed without error, appropriate \ref Error enum value if not.
*/
static Error execute_binary1(float& lhs, ngc_binary_op_t operation, const float& rhs) {
    Error status = Error::Ok;

    switch (operation) {
        case Binary_DividedBy:
            if (rhs == 0.0f || rhs == -0.0f)
                status = Error::ExpressionDivideByZero;  // Attempt to divide by zero
            else
                lhs = lhs / rhs;
            break;

        case Binary_Modulo:  // always calculates a positive answer
            lhs = fmodf(lhs, rhs);
            if (lhs < 0.0f)
                lhs = lhs + fabsf(rhs);
            break;

        case Binary_Power:
            if (lhs < 0.0f && floorf(rhs) != rhs)
                status = Error::ExpressionInvalidArgument;  // Attempt to raise negative value to non-integer power
            else
                lhs = powf(lhs, rhs);
            break;

        case Binary_Times:
            lhs = lhs * rhs;
            break;

        default:
            status = Error::ExpressionUnknownOp;
    }

    if (status != Error::Ok)
        report_param_error(status);

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
static Error execute_binary2(float& lhs, ngc_binary_op_t operation, const float& rhs) {
    switch (operation) {
        case Binary_And2:
            lhs = ((lhs == 0.0f) || (rhs == 0.0f)) ? 0.0f : 1.0f;
            break;

        case Binary_ExclusiveOR:
            lhs = (((lhs == 0.0f) && (rhs != 0.0f)) || ((lhs != 0.0f) && (rhs == 0.0f))) ? 1.0f : 0.0f;
            break;

        case Binary_Minus:
            lhs = (lhs - rhs);
            break;

        case Binary_NotExclusiveOR:
            lhs = ((lhs != 0.0f) || (rhs != 0.0f)) ? 1.0f : 0.0f;
            break;

        case Binary_Plus:
            lhs = (lhs + rhs);
            break;

        case Binary_LT:
            lhs = (lhs < rhs) ? 1.0f : 0.0f;
            break;

        case Binary_EQ: {
            float diff = lhs - rhs;
            diff       = (diff < 0.0f) ? -diff : diff;
            lhs        = (diff < TOLERANCE_EQUAL) ? 1.0f : 0.0f;
        } break;

        case Binary_NE: {
            float diff = lhs - rhs;
            diff       = (diff < 0.0f) ? -diff : diff;
            lhs        = (diff >= TOLERANCE_EQUAL) ? 1.0f : 0.0f;
        } break;

        case Binary_LE:
            lhs = (lhs <= rhs) ? 1.0f : 0.0f;
            break;

        case Binary_GE:
            lhs = (lhs >= rhs) ? 1.0f : 0.0f;
            break;

        case Binary_GT:
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
static Error execute_binary(float& lhs, ngc_binary_op_t operation, const float& rhs) {
    if (operation <= Binary_Binary2)
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
        case Unary_ABS:
            if (operand < 0.0f)
                operand = (-1.0f * operand);
            break;

        case Unary_ACOS:
            if (operand < -1.0f || operand > 1.0f)
                status = Error::ExpressionArgumentOutOfRange;  // Argument to ACOS out of range
            else
                operand = acosf(operand) * DEGRAD;
            break;

        case Unary_ASIN:
            if (operand < -1.0f || operand > 1.0f)
                status = Error::ExpressionArgumentOutOfRange;  // Argument to ASIN out of range
            else
                operand = asinf(operand) * DEGRAD;
            break;

        case Unary_COS:
            operand = cosf(operand * RADDEG);
            break;

        case Unary_Exists:
            // do nothing here, result for the EXISTS function is set by read_unary()
            break;

        case Unary_EXP:
            operand = expf(operand);
            break;

        case Unary_FIX:
            operand = floorf(operand);
            break;

        case Unary_FUP:
            operand = ceilf(operand);
            break;

        case Unary_LN:
            if (operand <= 0.0f)
                status = Error::ExpressionArgumentOutOfRange;  // Argument to LN out of range
            else
                operand = logf(operand);
            break;

        case Unary_Round:
            operand = (float)((int)(operand + ((operand < 0.0f) ? -0.5f : 0.5f)));
            break;

        case Unary_SIN:
            operand = sinf(operand * RADDEG);
            break;

        case Unary_SQRT:
            if (operand < 0.0f)
                status = Error::ExpressionArgumentOutOfRange;  // Negative argument to SQRT
            else
                operand = sqrtf(operand);
            break;

        case Unary_TAN:
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
        case Binary_RightBracket:
            return 1;

        case Binary_And2:
        case Binary_ExclusiveOR:
        case Binary_NotExclusiveOR:
            return 2;

        case Binary_LT:
        case Binary_EQ:
        case Binary_NE:
        case Binary_LE:
        case Binary_GE:
        case Binary_GT:
            return 3;

        case Binary_Minus:
        case Binary_Plus:
            return 4;

        case Binary_NoOp:
        case Binary_DividedBy:
        case Binary_Modulo:
        case Binary_Times:
            return 5;

        case Binary_Power:
            return 6;

        default:
            break;
    }

    return 0;  // should never happen
}

#if 0
// For possible later code simplification
std::map<std::string, ngc_cmd_t, std::less<>> binary_ops = {
    { "+", Binary_Plus },
    { "-", Binary_Minus },
    { "/", Binary_DividedBy },
    { "*", Binary_Times },
    { "**", Binary_Power },
    { "]", Binary_RightBracket },
    { "AND", Binary_And2 },
    { "MOD", Binary_Mod },
    { "OR", Binary_NotExclusiveOr },
    { "XOR", Binary_ExclusiveOr },
    { "EQ", Binary_EQ },
    { "NE", Binary_NE },
    { "GE", Binary_GE },
    { "GT", Binary_GT },
};
std::map<std::string, ngc_cmd_t, std::less<>> unary_ops = {
    { "ABS", Unary_ABS },
    { "ACOS", Unary_ACOS },
    { "ASIN", Unary_ASIN },
    { "ATAN", Unary_ATAN },
    { "COS", Unary_COS },
    { "EXP", Unary_EXP },
    { "EXISTS", Unary_Exists },
    { "FIX", Unary_FIX },
    { "FUP", Unary_FUP },
    { "LN", Unary_LN },
    { "ROUND", Unary_ROUND },
    { "SIN", Unary_SIN },
    { "SQRT", Unary_SQRT },
    { "TAN", Unary_TAN },
};
#endif

/*! \brief Reads a binary operation out of the line
starting at the index given by the pos offset. If a valid one is found, the
value of operation is set to the symbolic value for that operation.

\param line pointer to RS274/NGC code (block).
\param pos offset into line where expression starts.
\param operation pointer to \ref ngc_binary_op_t enum value.
\returns #Error::Ok enum value if processed without error, appropriate \ref Error enum value if not.
*/
static Error read_operation(const char* line, size_t& pos, ngc_binary_op_t& operation) {
    char  c      = line[pos];
    Error status = Error::Ok;

    pos++;

    switch (c) {
        case '+':
            operation = Binary_Plus;
            break;

        case '-':
            operation = Binary_Minus;
            break;

        case '/':
            operation = Binary_DividedBy;
            break;

        case '*':
            if (line[pos] == '*') {
                operation = Binary_Power;
                pos++;
            } else
                operation = Binary_Times;
            break;

        case ']':
            operation = Binary_RightBracket;
            break;

        case 'A':
            if (!strncmp(line + pos, "ND", 2)) {
                operation = Binary_And2;
                pos += 2;
            } else
                status = Error::ExpressionUnknownOp;  // Unknown operation name starting with A
            break;

        case 'M':
            if (!strncmp(line + pos, "OD", 2)) {
                operation = Binary_Modulo;
                pos += 2;
            } else
                status = Error::ExpressionUnknownOp;  // Unknown operation name starting with M
            break;

        case 'O':
            if (line[pos] == 'R') {
                operation = Binary_NotExclusiveOR;
                pos++;
            } else
                status = Error::ExpressionUnknownOp;  // Unknown operation name starting with R
            break;

        case 'X':
            if (!strncmp(line + pos, "OR", 2)) {
                operation = Binary_ExclusiveOR;
                pos += 2;
            } else
                status = Error::ExpressionUnknownOp;  // Unknown operation name starting with X
            break;

        /* relational operators */
        case 'E':
            if (line[pos] == 'Q') {
                operation = Binary_EQ;
                pos++;
            } else
                status = Error::ExpressionUnknownOp;  // Unknown operation name starting with E
            break;

        case 'N':
            if (line[pos] == 'E') {
                operation = Binary_NE;
                pos++;
            } else
                status = Error::ExpressionUnknownOp;  // Unknown operation name starting with N
            break;

        case 'G':
            if (line[pos] == 'E') {
                operation = Binary_GE;
                pos++;
            } else if (line[pos] == 'T') {
                operation = Binary_GT;
                pos++;
            } else
                status = Error::ExpressionUnknownOp;  // Unknown operation name starting with G
            break;

        case 'L':
            if (line[pos] == 'E') {
                operation = Binary_LE;
                pos++;
            } else if (line[pos] == 'T') {
                operation = Binary_LT;
                pos++;
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
static Error read_operation_unary(const char* line, size_t& pos, ngc_unary_op_t& operation) {
    char  c      = line[pos];
    Error status = Error::Ok;

    pos++;

    switch (c) {
        case 'A':
            if (!strncmp(line + pos, "BS", 2)) {
                operation = Unary_ABS;
                pos += 2;
            } else if (!strncmp(line + pos, "COS", 3)) {
                operation = Unary_ACOS;
                pos += 3;
            } else if (!strncmp(line + pos, "SIN", 3)) {
                operation = Unary_ASIN;
                pos += 3;
            } else if (!strncmp(line + pos, "TAN", 3)) {
                operation = Unary_ATAN;
                pos += 3;
            } else
                status = Error::ExpressionUnknownOp;
            break;

        case 'C':
            if (!strncmp(line + pos, "OS", 2)) {
                operation = Unary_COS;
                pos += 2;
            } else
                status = Error::ExpressionUnknownOp;
            break;

        case 'E':
            if (!strncmp(line + pos, "XP", 2)) {
                operation = Unary_EXP;
                pos += 2;
            } else if (!strncmp(line + pos, "XISTS", 5)) {
                operation = Unary_Exists;
                pos += 5;
            } else
                status = Error::ExpressionUnknownOp;
            break;

        case 'F':
            if (!strncmp(line + pos, "IX", 2)) {
                operation = Unary_FIX;
                pos += 2;
            } else if (!strncmp(line + pos, "UP", 2)) {
                operation = Unary_FUP;
                pos += 2;
            } else
                status = Error::ExpressionUnknownOp;
            break;

        case 'L':
            if (line[pos] == 'N') {
                operation = Unary_LN;
                pos++;
            } else
                status = Error::ExpressionUnknownOp;
            break;

        case 'R':
            if (!strncmp(line + pos, "OUND", 4)) {
                operation = Unary_Round;
                pos += 4;
            } else
                status = Error::ExpressionUnknownOp;
            break;

        case 'S':
            if (!strncmp(line + pos, "IN", 2)) {
                operation = Unary_SIN;
                pos += 2;
            } else if (!strncmp((line + pos), "QRT", 3)) {
                operation = Unary_SQRT;
                pos += 3;
            } else
                status = Error::ExpressionUnknownOp;
            break;

        case 'T':
            if (!strncmp(line + pos, "AN", 2)) {
                operation = Unary_TAN;
                pos += 2;
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
static Error read_atan(const char* line, size_t& pos, float& value) {
    float argument2;

    if (line[pos] != '/')
        return Error::ExpressionSyntaxError;  // Slash missing after first ATAN argument

    pos++;

    if (line[pos] != '[')
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
// cppcheck-suppress unusedFunction
Error read_unary(const char* line, size_t& pos, float& value) {
    ngc_unary_op_t operation;
    Error          status;

    if ((status = read_operation_unary(line, pos, operation)) != Error::Ok) {
        return status;
    }
    if (line[pos] != '[') {
        return Error::ExpressionSyntaxError;  // Left bracket missing after unary operation name
    }
    if (operation == Unary_Exists) {
        ++pos;
        std::string arg;
        char        c;
        while ((c = line[pos]) && c != ']') {
            ++pos;
            arg += c;
        }
        if (!c) {
            return Error::ExpressionSyntaxError;
        }
        ++pos;
        value = named_param_exists(arg) ? 1.0 : 0.0;
        return Error::Ok;
    }
    if ((status = expression(line, pos, value)) != Error::Ok) {
        return status;
    }
    if (operation == Unary_ATAN) {
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
Error expression(const char* line, size_t& pos, float& value) {
    float           values[MAX_STACK];
    ngc_binary_op_t operators[MAX_STACK];
    uint_fast8_t    stack_index = 1;

    if (line[pos] != '[')
        return Error::GcodeUnsupportedCommand;

    pos++;

    Error status;

    if ((!read_number(line, pos, values[0], true)))
        return Error::BadNumberFormat;

    if ((status = read_operation(line, pos, operators[0])) != Error::Ok)
        return status;

    for (; operators[0] != Binary_RightBracket;) {
        if ((!read_number(line, pos, values[stack_index], true)))
            return Error::BadNumberFormat;

        if ((status = read_operation(line, pos, operators[stack_index])) != Error::Ok)
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
