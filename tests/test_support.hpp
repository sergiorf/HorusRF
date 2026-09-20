#pragma once

#include <sstream>
#include <stdexcept>
#include <string>

namespace test_support {

inline void check(bool condition, const char* expression, const char* file, int line) {
    if (condition) return;
    std::ostringstream message;
    message << file << ':' << line << ": check failed: " << expression;
    throw std::runtime_error(message.str());
}

template <typename Actual, typename Expected>
void check_equal(const Actual& actual, const Expected& expected, const char* actual_text,
                 const char* expected_text, const char* file, int line) {
    if (actual == expected) return;
    std::ostringstream message;
    message << file << ':' << line << ": expected " << actual_text << " == " << expected_text;
    throw std::runtime_error(message.str());
}

} // namespace test_support

#define CHECK(expression) \
    ::test_support::check(static_cast<bool>(expression), #expression, __FILE__, __LINE__)
#define CHECK_EQ(actual, expected) \
    ::test_support::check_equal((actual), (expected), #actual, #expected, __FILE__, __LINE__)
