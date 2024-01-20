#pragma once

static constexpr bool verbose_debugging = false;
#define log_parser_verbose(x)                                                                                                              \
    do {                                                                                                                                   \
        if (verbose_debugging) {                                                                                                           \
            log_debug(x);                                                                                                                  \
        }                                                                                                                                  \
    } while (0)
