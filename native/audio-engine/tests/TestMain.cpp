#include "TestFramework.h"
#include <chrono>
#include <cstring>

int main (int argc, char** argv)
{
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int ran = 0, failedTests = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (const auto& t : vt::registry())
    {
        if (filter && std::strstr (t.name, filter) == nullptr) continue;
        const int before = vt::failures();
        const auto s = std::chrono::steady_clock::now();
        std::printf ("[ RUN  ] %s\n", t.name);
        std::fflush (stdout);
        try { t.fn(); }
        catch (const std::exception& e) { vt::report (false, "exception", t.name, 0, e.what()); }
        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - s).count();
        const bool ok = vt::failures() == before;
        std::printf ("[ %s ] %s (%.0f ms)\n", ok ? " OK " : "FAIL", t.name, ms);
        ++ran;
        if (! ok) ++failedTests;
    }
    const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    std::printf ("\n%d tests, %d checks, %d failed tests, %d failed checks (%.1f s)\n", ran, vt::checks(), failedTests, vt::failures(), sec);
    return failedTests == 0 ? 0 : 1;
}
