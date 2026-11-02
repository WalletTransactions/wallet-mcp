#include "metrics2.hpp"

#include <cstdint>
#include <map>
#include <mutex>
#include <sstream>
#include <utility>

namespace metrics2 {

namespace {

struct Family {
    std::string help;
    std::string label1;
    std::string label2;
    std::map<std::pair<std::string, std::string>, std::uint64_t> values;
};

std::mutex g_mutex;
std::map<std::string, Family> g_families;

std::string escape_label(const std::string& value) {
    std::string out;
    for (char c : value) {
        if (c == '\\' || c == '"') out.push_back('\\');
        if (c == '\n') {
            out += "\\n";
            continue;
        }
        out.push_back(c);
    }
    return out;
}

} // namespace

void inc_counter(const std::string& name, const std::string& help, const std::string& label1,
                 const std::string& value1, const std::string& label2, const std::string& value2) {
    std::lock_guard<std::mutex> lock(g_mutex);
    Family& f = g_families[name];
    f.help = help;
    f.label1 = label1;
    f.label2 = label2;
    f.values[{value1, value2}]++;
}

std::string render() {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::ostringstream out;
    for (const auto& [name, f] : g_families) {
        out << "# HELP " << name << " " << f.help << "\n";
        out << "# TYPE " << name << " counter\n";
        for (const auto& [labels, count] : f.values) {
            out << name << "{" << f.label1 << "=\"" << escape_label(labels.first) << "\"," << f.label2 << "=\""
                << escape_label(labels.second) << "\"} " << count << "\n";
        }
    }
    return out.str();
}

} // namespace metrics2
