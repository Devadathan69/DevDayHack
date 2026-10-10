#pragma once
#include <array>
#include <string>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cerrno>

namespace NpcPedMetadata {
using Table = std::array<unsigned char, 65536>;
inline std::string Trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}
inline std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}
inline unsigned char StockModelGender(const std::string& model) {
    // Stock stems encode race, sex (f/m), and age (y/o); custom/ambiguous names stay unknown.
    for (const std::string prefix : {std::string{}, std::string{"s"}, std::string{"c"}, std::string{"cs"}}) {
        const size_t start = prefix.size();
        if (model.size() < start + 3 || model.compare(0, start, prefix) != 0) continue;
        const char race = model[start], sex = model[start + 1], age = model[start + 2];
        if ((race == 'b' || race == 'w' || race == 'h' || race == 'a') && (age == 'y' || age == 'o')) {
            if (sex == 'f') return 2;
            if (sex == 'm') return 1;
        }
    }
    return 0;
}
inline Table Parse(const std::string& data) {
    Table table{};
    std::istringstream lines(data);
    std::string line;
    while (std::getline(lines, line)) {
        line = line.substr(0, line.find('#'));
        std::istringstream fields(line);
        std::array<std::string, 6> values;
        bool complete = true;
        for (auto& value : values) {
            if (!std::getline(fields, value, ',')) { complete = false; break; }
            value = Trim(value);
        }
        if (!complete) continue;
        char* end = nullptr;
        errno = 0;
        const long id = std::strtol(values[0].c_str(), &end, 10);
        if (errno == ERANGE || end == values[0].c_str() || *end || id < 0 || id >= 65536) continue;
        const auto type = Lower(values[3]);
        const auto animation = Lower(values[5]);
        const bool femaleAnimation = animation == "woman" || animation == "busywoman" ||
            animation == "sexywoman" || animation == "fatwoman" || animation == "oldwoman";
        const bool maleAnimation = animation == "man" || animation == "fatman" || animation == "oldman";
        const unsigned char stock = StockModelGender(Lower(values[1]));
        const unsigned char gender = type == "civfemale" || femaleAnimation || stock == 2 ? 2 :
            type == "civmale" || maleAnimation || stock == 1 ? 1 : 0;
        table[static_cast<size_t>(id)] = gender;
    }
    return table;
}
}
