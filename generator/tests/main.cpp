#include "check.hpp"

#include <exception>
#include <iostream>

namespace check {

std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

}  // namespace check

int main(int argc, char** argv) {
    const std::string filter = argc > 1 ? argv[1] : std::string();

    int passed = 0;
    int failed = 0;
    int skipped = 0;

    for (const check::Case& test : check::registry()) {
        if (!filter.empty() && test.name.find(filter) == std::string::npos) {
            ++skipped;
            continue;
        }
        try {
            test.body();
            std::cout << "  ok   " << test.name << '\n';
            ++passed;
        } catch (const check::Failure& failure) {
            std::cout << "  FAIL " << test.name << "\n         " << failure.message << '\n';
            ++failed;
        } catch (const std::exception& error) {
            std::cout << "  FAIL " << test.name << "\n         threw: " << error.what() << '\n';
            ++failed;
        }
    }

    std::cout << '\n' << passed << " passed, " << failed << " failed";
    if (skipped) std::cout << ", " << skipped << " filtered out";
    std::cout << '\n';

    if (passed == 0 && failed == 0) {
        std::cout << "no tests ran -- that is a failure, not a pass\n";
        return 1;
    }
    return failed == 0 ? 0 : 1;
}
