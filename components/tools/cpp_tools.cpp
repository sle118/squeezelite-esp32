#include "cpp_tools.h"
#include <cctype>
#include "tools.h"

std::string trim(const std::string& str) {
    const size_t first = str.find_first_not_of(' ');
    if (first == std::string::npos) {
        // String contains only spaces or is empty
        return "";
    }
    const size_t last = str.find_last_not_of(' ');
    return str.substr(first, (last - first + 1));
}

std::string& toLowerStr(std::string& str) {
    for (auto& c : str) {
        if (c >= 'A' && c <= 'Z') {
            c = c - 'A' + 'a';
        }
    }
    return str;
}
