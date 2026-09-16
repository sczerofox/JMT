#include "console/repl_utils.hpp"
#include <vector>

std::vector<std::wstring> ReplUtils::splitCommandLine(const std::wstring& line) {
    std::vector<std::wstring> args;
    std::wstring current;
    bool inQuotes = false;
    for (wchar_t ch : line) {
        if (ch == L'"') {
            inQuotes = !inQuotes;
        } else if (ch == L' ' && !inQuotes) {
            if (!current.empty()) {
                args.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty())
        args.push_back(current);
    return args;
}