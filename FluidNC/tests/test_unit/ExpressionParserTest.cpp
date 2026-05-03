// Test suite for read_number() function
// Validates Phase 1 implementation: unary operators on non-numeric values
// Tests parser syntax without complex system dependencies

#include "gtest/gtest.h"
#include "LinuxCNCParser.h"
#include "Parameters.h"
#include <cmath>

namespace {

static bool approx_equal(float a, float b, float tolerance = 1e-5f) {
    return std::abs(a - b) < tolerance;
}

// ============================================================================
// Basic Numeric Literals
// ============================================================================

TEST(ReadNumberTest, PositiveInteger) {
    float  result;
    size_t pos = 0;
    EXPECT_TRUE(read_number("123", pos, result));
    EXPECT_TRUE(approx_equal(result, 123.0f));
}

TEST(ReadNumberTest, NegativeInteger) {
    float  result;
    size_t pos = 0;
    EXPECT_TRUE(read_number("-123", pos, result));
    EXPECT_TRUE(approx_equal(result, -123.0f));
}

TEST(ReadNumberTest, Float) {
    float  result;
    size_t pos = 0;
    EXPECT_TRUE(read_number("123.456", pos, result));
    EXPECT_TRUE(approx_equal(result, 123.456f));
}

TEST(ReadNumberTest, NegativeFloat) {
    float  result;
    size_t pos = 0;
    EXPECT_TRUE(read_number("-123.456", pos, result));
    EXPECT_TRUE(approx_equal(result, -123.456f));
}

TEST(ReadNumberTest, LeadingDecimal) {
    float  result;
    size_t pos = 0;
    EXPECT_TRUE(read_number(".5", pos, result));
    EXPECT_TRUE(approx_equal(result, 0.5f));
}

TEST(ReadNumberTest, NegativeLeadingDecimal) {
    float  result;
    size_t pos = 0;
    EXPECT_TRUE(read_number("-.5", pos, result));
    EXPECT_TRUE(approx_equal(result, -0.5f));
}

TEST(ReadNumberTest, UnaryPlusNumeric) {
    float  result;
    size_t pos = 0;
    EXPECT_TRUE(read_number("+123", pos, result));
    EXPECT_TRUE(approx_equal(result, 123.0f));
}

TEST(ReadNumberTest, Zero) {
    float  result;
    size_t pos = 0;
    EXPECT_TRUE(read_number("0", pos, result));
    EXPECT_TRUE(approx_equal(result, 0.0f));
}

// ============================================================================
// Bracketed Expressions
// ============================================================================

TEST(ReadNumberTest, SimpleExpression) {
    float  result;
    size_t pos = 0;
    EXPECT_TRUE(read_number("[2+3]", pos, result));
    EXPECT_TRUE(approx_equal(result, 5.0f));
}

TEST(ReadNumberTest, ExpressionWithMultiplication) {
    float  result;
    size_t pos = 0;
    EXPECT_TRUE(read_number("[2*3+4]", pos, result));
    EXPECT_TRUE(approx_equal(result, 10.0f));
}

TEST(ReadNumberTest, NegatedExpression) {
    float  result;
    size_t pos = 0;
    EXPECT_TRUE(read_number("-[2+3]", pos, result));
    EXPECT_TRUE(approx_equal(result, -5.0f));
}

TEST(ReadNumberTest, UnaryPlusExpression) {
    float  result;
    size_t pos = 0;
    EXPECT_TRUE(read_number("+[2+3]", pos, result));
    EXPECT_TRUE(approx_equal(result, 5.0f));
}

TEST(ReadNumberTest, ExpressionWithPower) {
    float  result;
    size_t pos = 0;
    EXPECT_TRUE(read_number("[2**3]", pos, result));
    EXPECT_TRUE(approx_equal(result, 8.0f));
}

TEST(ReadNumberTest, NegatedPowerExpression) {
    float  result;
    size_t pos = 0;
    EXPECT_TRUE(read_number("-[2**3]", pos, result));
    EXPECT_TRUE(approx_equal(result, -8.0f));
}

// ============================================================================
// Unary Functions (in expressions only)
// ============================================================================

TEST(ReadNumberTest, FunctionAbsoluteValue) {
    float  result;
    size_t pos = 0;
    EXPECT_TRUE(read_number("[ABS[-5]]", pos, result));
    EXPECT_TRUE(approx_equal(result, 5.0f));
}

TEST(ReadNumberTest, FunctionSquareRoot) {
    float  result;
    size_t pos = 0;
    EXPECT_TRUE(read_number("[SQRT[16]]", pos, result));
    EXPECT_TRUE(approx_equal(result, 4.0f));
}

TEST(ReadNumberTest, FunctionRound) {
    float  result;
    size_t pos = 0;
    EXPECT_TRUE(read_number("[ROUND[3.7]]", pos, result));
    EXPECT_TRUE(approx_equal(result, 4.0f));
}

TEST(ReadNumberTest, NegatedFunctionInExpression) {
    float  result;
    size_t pos = 0;
    EXPECT_TRUE(read_number("[-ABS[-5]]", pos, result));
    EXPECT_TRUE(approx_equal(result, -5.0f));
}

// ============================================================================
// LinuxCNC Extension: Unary Operators on Non-Numeric Values
// This is the key Phase 1 feature: unary minus/plus on bracketed expressions
// ============================================================================

TEST(ReadNumberTest, UnaryMinusOnBracketedExpression) {
    float  result;
    size_t pos = 0;
    // -[10+5] should equal -15
    // This is the Phase 1 key feature: unary operator recognized before '['
    EXPECT_TRUE(read_number("-[10+5]", pos, result));
    EXPECT_TRUE(approx_equal(result, -15.0f));
}

TEST(ReadNumberTest, UnaryPlusOnBracketedExpression) {
    float  result;
    size_t pos = 0;
    // +[10+5] should equal 15
    EXPECT_TRUE(read_number("+[10+5]", pos, result));
    EXPECT_TRUE(approx_equal(result, 15.0f));
}

TEST(ReadNumberTest, DoublyNegatedBracketedExpression) {
    float  result;
    size_t pos = 0;
    // --[7] should equal 7
    EXPECT_TRUE(read_number("--[7]", pos, result));
    EXPECT_TRUE(approx_equal(result, 7.0f));
}

TEST(ReadNumberTest, UnaryMinusThenPlus) {
    float  result;
    size_t pos = 0;
    // -+[5] should equal -5
    EXPECT_TRUE(read_number("-+[5]", pos, result));
    EXPECT_TRUE(approx_equal(result, -5.0f));
}

// ============================================================================
// Guard Condition: Negative Literals vs Unary Operators
// Key: Unary operators only recognized if next char is NOT digit or '.'
// ============================================================================

TEST(ReadNumberTest, NegativeDigitParsedAsLiteral) {
    float  result;
    size_t pos = 0;
    // -123 should be parsed as negative literal (digit follows operator)
    EXPECT_TRUE(read_number("-123", pos, result));
    EXPECT_TRUE(approx_equal(result, -123.0f));
}

TEST(ReadNumberTest, NegativeDecimalParsedAsLiteral) {
    float  result;
    size_t pos = 0;
    // -.456 should be parsed as negative literal (dot follows operator)
    EXPECT_TRUE(read_number("-.456", pos, result));
    EXPECT_TRUE(approx_equal(result, -0.456f));
}

// ============================================================================
// Complex Expressions
// ============================================================================

TEST(ReadNumberTest, ComplexExpressionPrecedence) {
    float  result;
    size_t pos = 0;
    // [2+3*4-5] should equal 9 (multiplication has higher precedence)
    EXPECT_TRUE(read_number("[2+3*4-5]", pos, result));
    EXPECT_TRUE(approx_equal(result, 9.0f));
}

TEST(ReadNumberTest, NegatedComplexExpression) {
    float  result;
    size_t pos = 0;
    // -[2+3*4] should equal -14
    EXPECT_TRUE(read_number("-[2+3*4]", pos, result));
    EXPECT_TRUE(approx_equal(result, -14.0f));
}

// ============================================================================
// Position Tracking
// ============================================================================

TEST(ReadNumberTest, PositionAdvancedAfterNumber) {
    float       result;
    const char* input = "123+456";
    size_t      pos   = 0;
    EXPECT_TRUE(read_number(input, pos, result));
    EXPECT_EQ(pos, 3);  // Should be at the '+' character
    EXPECT_TRUE(approx_equal(result, 123.0f));
}

TEST(ReadNumberTest, PositionAdvancedAfterExpression) {
    float       result;
    const char* input = "[1+2]+3";
    size_t      pos   = 0;
    EXPECT_TRUE(read_number(input, pos, result));
    EXPECT_EQ(pos, 5);  // Should be at the '+' character after ']'
    EXPECT_TRUE(approx_equal(result, 3.0f));
}

TEST(ReadNumberTest, PositionAdvancedAfterUnaryOperator) {
    float       result;
    const char* input = "-[5]+2";
    size_t      pos   = 0;
    EXPECT_TRUE(read_number(input, pos, result));
    EXPECT_EQ(pos, 4);  // Should be at the '+' character
    EXPECT_TRUE(approx_equal(result, -5.0f));
}

// ============================================================================
// Parameter References
// ============================================================================

TEST(ReadNumberTest, ParameterReference) {
    float  result;
    size_t pos = 0;
    set_numbered_param(1, 42.5f);
    EXPECT_TRUE(read_number("#1", pos, result));
    EXPECT_TRUE(approx_equal(result, 42.5f));
}

TEST(ReadNumberTest, NegatedParameterReference) {
    float  result;
    size_t pos = 0;
    set_numbered_param(2, 100.0f);
    // -#2 should equal -100 (Phase 1: unary operator on parameter)
    EXPECT_TRUE(read_number("-#2", pos, result));
    EXPECT_TRUE(approx_equal(result, -100.0f));
}

TEST(ReadNumberTest, UnaryPlusParameterReference) {
    float  result;
    size_t pos = 0;
    set_numbered_param(3, 50.0f);
    EXPECT_TRUE(read_number("+#3", pos, result));
    EXPECT_TRUE(approx_equal(result, 50.0f));
}

TEST(ReadNumberTest, DoublyNegatedParameter) {
    float  result;
    size_t pos = 0;
    set_numbered_param(4, 25.0f);
    // --#4 should equal 25
    EXPECT_TRUE(read_number("--#4", pos, result));
    EXPECT_TRUE(approx_equal(result, 25.0f));
}

// ============================================================================
// String View Overload (Phase 1 bug fix)
// ============================================================================

TEST(ReadNumberTest, StringViewOverload) {
    float            result;
    std::string_view sv("123.45");
    EXPECT_TRUE(read_number(sv, result));
    EXPECT_TRUE(approx_equal(result, 123.45f));
}

TEST(ReadNumberTest, StringViewWithUnaryOperator) {
    float result;
    set_numbered_param(30, 77.0f);
    std::string_view sv("-#30");
    EXPECT_TRUE(read_number(sv, result));
    EXPECT_TRUE(approx_equal(result, -77.0f));
}

TEST(ReadNumberTest, StringViewWithNegatedExpression) {
    float            result;
    std::string_view sv("-[10+20]");
    EXPECT_TRUE(read_number(sv, result));
    EXPECT_TRUE(approx_equal(result, -30.0f));
}

// ============================================================================
// Phase 3: Comparative Tests Against LinuxCNC
// ============================================================================

TEST(ComparativeTest, BasicNumericLiterals) {
    const char *test_cases[] = {"123", "-456", "78.9", "-12.34"};
    
    for (const char *test : test_cases) {
        float   fluidnc_result;
        double  linuxcnc_result;
        size_t  fluidnc_pos = 0;
        int     linuxcnc_pos = 0;
        
        bool fluidnc_ok = read_number(test, fluidnc_pos, fluidnc_result);
        int  linuxcnc_ok = linuxcnc_read_value(test, &linuxcnc_pos, &linuxcnc_result);
        
        EXPECT_TRUE(fluidnc_ok);
        EXPECT_EQ(linuxcnc_ok, 0);  // LinuxCNC returns 0 for success
        EXPECT_FLOAT_EQ(fluidnc_result, (float)linuxcnc_result);
        EXPECT_EQ(fluidnc_pos, (size_t)linuxcnc_pos);
    }
}

TEST(ComparativeTest, UnaryOperatorsOnExpressions) {
    const char *test_cases[] = {
        "-[10+5]",   // Phase 1: unary minus on bracketed expression
        "+[20*2]",   // Phase 1: unary plus on expression
        "-[100]",    // Unary minus on simple bracketed value
        "+[50]",     // Unary plus on simple bracketed value
        "--[7]",     // Double negation
    };
    
    for (const char *test : test_cases) {
        float   fluidnc_result;
        double  linuxcnc_result;
        size_t  fluidnc_pos = 0;
        int     linuxcnc_pos = 0;
        
        bool fluidnc_ok = read_number(test, fluidnc_pos, fluidnc_result);
        int  linuxcnc_ok = linuxcnc_read_value(test, &linuxcnc_pos, &linuxcnc_result);
        
        EXPECT_TRUE(fluidnc_ok) << "FluidNC failed on: " << test;
        EXPECT_EQ(linuxcnc_ok, 0) << "LinuxCNC failed on: " << test;
        EXPECT_FLOAT_EQ(fluidnc_result, (float)linuxcnc_result)
            << "Mismatch on: " << test;
    }
}

TEST(ComparativeTest, UnaryFunctions) {
    const char *test_cases[] = {
        "[ABS[-5]]",     // Absolute value
        "[SQRT[16]]",    // Square root
        "[SIN[0]]",      // Sine of 0 should be 0
        "[COS[0]]",      // Cosine of 0 should be 1
    };
    
    for (const char *test : test_cases) {
        float   fluidnc_result;
        double  linuxcnc_result;
        size_t  fluidnc_pos = 0;
        int     linuxcnc_pos = 0;
        
        bool fluidnc_ok = read_number(test, fluidnc_pos, fluidnc_result);
        int  linuxcnc_ok = linuxcnc_read_value(test, &linuxcnc_pos, &linuxcnc_result);
        
        EXPECT_TRUE(fluidnc_ok) << "FluidNC failed on: " << test;
        EXPECT_EQ(linuxcnc_ok, 0) << "LinuxCNC failed on: " << test;
        EXPECT_FLOAT_EQ(fluidnc_result, (float)linuxcnc_result)
            << "Mismatch on: " << test;
    }
}

TEST(ComparativeTest, BinaryOperators) {
    const char *test_cases[] = {
        "[2+3]",       // Addition
        "[10-4]",      // Subtraction
        "[3*4]",       // Multiplication
        "[20/4]",      // Division
        "[2**3]",      // Power
        "[17MOD5]",    // Modulo
    };
    
    for (const char *test : test_cases) {
        float   fluidnc_result;
        double  linuxcnc_result;
        size_t  fluidnc_pos = 0;
        int     linuxcnc_pos = 0;
        
        bool fluidnc_ok = read_number(test, fluidnc_pos, fluidnc_result);
        int  linuxcnc_ok = linuxcnc_read_value(test, &linuxcnc_pos, &linuxcnc_result);
        
        EXPECT_TRUE(fluidnc_ok) << "FluidNC failed on: " << test;
        EXPECT_EQ(linuxcnc_ok, 0) << "LinuxCNC failed on: " << test;
        EXPECT_FLOAT_EQ(fluidnc_result, (float)linuxcnc_result)
            << "Mismatch on: " << test;
    }
}

TEST(ComparativeTest, OperatorPrecedence) {
    const char *test_cases[] = {
        "[2+3*4]",       // * has higher precedence than +
        "[10-2*3]",      // * has higher precedence than -
        "[2**3*2]",      // ** has higher precedence than *
        "[2+3*4-5]",     // Complex: should be 2 + 12 - 5 = 9
    };
    
    for (const char *test : test_cases) {
        float   fluidnc_result;
        double  linuxcnc_result;
        size_t  fluidnc_pos = 0;
        int     linuxcnc_pos = 0;
        
        bool fluidnc_ok = read_number(test, fluidnc_pos, fluidnc_result);
        int  linuxcnc_ok = linuxcnc_read_value(test, &linuxcnc_pos, &linuxcnc_result);
        
        EXPECT_TRUE(fluidnc_ok) << "FluidNC failed on: " << test;
        EXPECT_EQ(linuxcnc_ok, 0) << "LinuxCNC failed on: " << test;
        EXPECT_FLOAT_EQ(fluidnc_result, (float)linuxcnc_result)
            << "Mismatch on: " << test;
    }
}

}  // namespace
