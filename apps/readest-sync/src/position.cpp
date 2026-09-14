#include "probe.h"

#include <cctype>
#include <stdexcept>

namespace readest {
namespace {
bool assertion(const std::string& s, size_t& at) {
    if (s[at] != '[') return true;
    ++at;
    while (at < s.size()) {
        char c = s[at++];
        if (c == '^') {
            if (at == s.size() || std::string("^[](),;=").find(s[at++]) == std::string::npos)
                return false;
        } else if (c == ']') {
            return true;
        } else if (c == '[' || c == '(' || c == ')') {
            return false;
        }
    }
    return false;
}

bool number(const std::string& s, size_t& at, bool zero_allowed) {
    size_t start = at;
    bool nonzero = false;
    while (at < s.size() && s[at] >= '0' && s[at] <= '9') {
        nonzero = nonzero || s[at] != '0';
        ++at;
    }
    return at > start && (zero_allowed || nonzero);
}

bool path(const std::string& s, size_t& at) {
    size_t start = at;
    while (at < s.size() && s[at] == '/') {
        ++at;
        if (!number(s, at, false)) return false;
        if (at < s.size() && !assertion(s, at)) return false;
    }
    return at > start;
}
} // namespace

std::string point_cfi(const std::string& position) {
    std::string s = position;
    const std::string native_prefix = "pbr:/webkit?##";
    if (s.compare(0, native_prefix.size(), native_prefix) == 0)
        s.erase(0, native_prefix.size());
    else if (!s.empty() && s[0] == '#')
        s.erase(0, 1);
    if (s.size() > 8192 || s.compare(0, 8, "epubcfi(") != 0) return "";
    for (unsigned char c : s)
        if (c < 32 || c == 127) return "";
    size_t at = 8;
    if (!path(s, at) || at >= s.size() || s[at++] != '!') return "";
    if (!path(s, at)) return "";
    if (at < s.size() && s[at] == ':') {
        ++at;
        if (!number(s, at, true)) return "";
        if (at < s.size() && !assertion(s, at)) return "";
    }
    return at + 1 == s.size() && s[at] == ')' ? s : "";
}

std::string readest_start_cfi(const std::string& s) {
    if (s.size() > 8192 || s.compare(0, 8, "epubcfi(") != 0 || s.back() != ')')
        return "";
    auto point = point_cfi(s);
    if (!point.empty()) return point;
    // Commas inside assertions are not range separators. Use the same
    // assertion grammar as point_cfi, including escaped brackets/commas.
    std::vector<size_t> commas;
    for (size_t at = 8; at + 1 < s.size();) {
        if (s[at] == '[') {
            if (!assertion(s, at)) return "";
        } else {
            if (s[at] == ',') commas.push_back(at);
            ++at;
        }
    }
    if (commas.size() != 2) return "";
    const auto parent = s.substr(0, commas[0]);
    size_t at = 8;
    if (!path(parent, at) || at >= parent.size() || parent[at++] != '!' ||
        !path(parent, at) || at != parent.size()) return "";
    const auto start = s.substr(commas[0] + 1, commas[1] - commas[0] - 1);
    const auto end = s.substr(commas[1] + 1, s.size() - commas[1] - 2);
    // A relative endpoint may extend the path or supply a text offset.
    // No side-bias parameters in ranges; spatial/temporal forms fail closed.
    // Readest itself rejects empty-start ranges as malformed saved locations.
    if (start.empty() || (start[0] != '/' && start[0] != ':') ||
        end.empty() || (end[0] != '/' && end[0] != ':') ||
        s.find(';') != std::string::npos) return "";
    const auto first = point_cfi(parent + start + ")");
    const auto last = point_cfi(parent + end + ")");
    if (first.empty() || last.empty() || compare_cfi(first, last) > 0) return "";
    return first;
}

int compare_cfi(const std::string& left, const std::string& right) {
    std::vector<std::vector<std::string>> paths;
    for (const auto& value : {left, right}) {
        auto s = point_cfi(value);
        if (s.empty()) s = readest_start_cfi(value);
        if (s.empty()) throw std::runtime_error("Cannot compare unsupported CFI");
        std::vector<std::string> numbers;
        for (size_t at = 8; at < s.size();) {
            if (s[at] == '[') { assertion(s, at); continue; }
            if (s[at] >= '0' && s[at] <= '9') {
                size_t begin = at;
                while (at < s.size() && s[at] >= '0' && s[at] <= '9') ++at;
                auto n = s.substr(begin, at - begin);
                auto first = n.find_first_not_of('0');
                numbers.push_back(first == std::string::npos ? "0" : n.substr(first));
            } else { ++at; }
        }
        paths.push_back(numbers);
    }
    for (size_t i = 0; i < paths[0].size() && i < paths[1].size(); ++i) {
        const auto &a = paths[0][i], &b = paths[1][i];
        if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
        if (a != b) return a < b ? -1 : 1;
    }
    return paths[0].size() == paths[1].size() ? 0 : paths[0].size() < paths[1].size() ? -1 : 1;
}
} // namespace readest
