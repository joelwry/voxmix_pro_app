#pragma once
// Minimal test framework (no dependencies).

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace vt
{
struct TestCase
{
    const char* name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry()
{
    static std::vector<TestCase> r;
    return r;
}

struct Registrar
{
    Registrar (const char* n, std::function<void()> f) { registry().push_back ({ n, std::move (f) }); }
};

inline int& failures() { static int f = 0; return f; }
inline int& checks() { static int c = 0; return c; }
inline std::string& currentTest() { static std::string s; return s; }

inline void report (bool ok, const char* expr, const char* file, int line, const std::string& msg = {})
{
    ++checks();
    if (! ok)
    {
        ++failures();
        std::printf ("    FAIL %s:%d: %s %s\n", file, line, expr, msg.c_str());
    }
}
} // namespace vt

#define VT_CAT2(a, b) a##b
#define VT_CAT(a, b) VT_CAT2 (a, b)
#define TEST(name) \
    static void name(); \
    static vt::Registrar VT_CAT (reg_, name) (#name, name); \
    static void name()

#define CHECK(expr) vt::report ((expr), #expr, __FILE__, __LINE__)
#define CHECK_MSG(expr, msg) vt::report ((expr), #expr, __FILE__, __LINE__, (msg))
#define CHECK_NEAR(a, b, tol) \
    vt::report (std::abs ((double) (a) - (double) (b)) <= (double) (tol), #a " ~ " #b, __FILE__, __LINE__, \
                "(" + std::to_string ((double) (a)) + " vs " + std::to_string ((double) (b)) + ", tol " + std::to_string ((double) (tol)) + ")")
