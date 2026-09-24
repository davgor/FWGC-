// Runs every registered test, or those whose name contains the argument.
#include <exception>
#include <iostream>
#include <string>

#include "check.hpp"

namespace check {
std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}
}  // namespace check

int main(int argc, char** argv) {
    const std::string filter = argc > 1 ? argv[1] : "";
    int passed = 0, failed = 0, skipped = 0;
    for (const check::Case& c : check::registry()) {
        if (!filter.empty() && c.name.find(filter) == std::string::npos) {
            ++skipped;
            continue;
        }
        try {
            c.body();
            ++passed;
            std::cout << "  ok   " << c.name << '\n';
        } catch (const check::Failure& f) {
            ++failed;
            std::cout << "  FAIL " << c.name << ": " << f.message << '\n';
        } catch (const std::exception& e) {
            ++failed;
            std::cout << "  FAIL " << c.name << ": threw " << e.what() << '\n';
        }
    }
    std::cout << '\n' << passed << " passed, " << failed << " failed";
    if (skipped) std::cout << ", " << skipped << " filtered out";
    std::cout << '\n';
    if (passed + failed == 0) {
        std::cout << "no test selected\n";
        return 1;
    }
    return failed == 0 ? 0 : 1;
}
