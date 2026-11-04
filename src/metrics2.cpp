#include "metrics2.hpp"

#include <cstdint>
#include <map>
#include <mutex>
#include <sstream>

namespace metrics2 {

namespace {

struct Family {
    std::string help;
    std::vector<std::string> names;
    std::map<std::vector<std::string>, std::uint64_t> values;
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

void inc_counter(const std::string& name, const std::string& help, const Labels& labels) {
    std::vector<std::string> names;
    std::vector<std::string> values;
    for (const auto& [k, v] : labels) {
        names.push_back(k);
        values.push_back(v);
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    Family& f = g_families[name];
    f.help = help;
    f.names = std::move(names);
    f.values[values]++;
}

std::string render() {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::ostringstream out;
    for (const auto& [name, f] : g_families) {
        out << "# HELP " << name << " " << f.help << "\n";
        out << "# TYPE " << name << " counter\n";
        for (const auto& [values, count] : f.values) {
            out << name << "{";
            for (std::size_t i = 0; i < values.size() && i < f.names.size(); ++i) {
                out << (i == 0 ? "" : ",") << f.names[i] << "=\"" << escape_label(values[i]) << "\"";
            }
            out << "} " << count << "\n";
        }
    }
    return out.str();
}

} // namespace metrics2
