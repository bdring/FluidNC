#include "FluidError.hpp"

namespace FluidErrorCategory {
    const char* error_names[] = { "None", "SDCard not configured" };
    namespace detail {
        class category : public std::error_category {
        public:
            virtual const char* name() const noexcept override { return "FluidError"; }
            virtual std::string message(int value) const override { return error_names[(int)value]; }
        };
    }
    const std::error_category& category() {
        // The category singleton
        static detail::category instance;
        return instance;
    }
}

std::error_code make_error_code(FluidError err) {
    return std::error_code(static_cast<int>(err), FluidErrorCategory::category());
}
