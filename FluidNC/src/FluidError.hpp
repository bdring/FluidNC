#include <system_error>

// The associated message strings are in FluidError.cpp
enum class FluidError { None, SDNotConfigured };

std::error_code make_error_code(FluidError);

// Declare that FluidError is a standard error code
// This makes it possible to assign a FluidError
// directly to std::error_code variable, e.g.
//   std::error_code ec = FluidError::SDNotConfigured

namespace std {
    template <>
    struct is_error_code_enum<FluidError> : true_type {};
}
