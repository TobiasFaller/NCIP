#include "kittenerror.hpp"

#include <stdarg.h>
#include <string>

extern "C" {

void kitten_error(const char *fmt, ...) {
    va_list ap;
    va_list ap2;
    va_start(ap, fmt);
    va_copy(ap, ap2);

    int size = std::vsnprintf(nullptr, 0, fmt, ap);
    va_end(ap);

    if (size <= 0) {
        va_end(ap2);
        throw KittenError { std::string("invalid format string: ") + fmt };
    }

    std::string message(static_cast<size_t>(size) + 1, ' ');
    std::vsnprintf(&message.front(), message.size(), fmt, ap);
    va_end(ap2);

    throw KittenError { message };
}

}