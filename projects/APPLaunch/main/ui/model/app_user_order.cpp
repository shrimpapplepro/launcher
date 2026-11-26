/*
 * SPDX-License-Identifier: MIT
 */

#include "app_user_order.hpp"

#include <algorithm>

namespace app_user_order {

std::vector<std::string> parse(std::string_view text)
{
    std::vector<std::string> names;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (!line.empty() && line.front() != '#' && rank(names, line) == names.size())
            names.emplace_back(line);
        start = end + 1;
    }
    return names;
}

std::string serialize(const std::vector<std::string> &names)
{
    std::string text = "# APPLaunch home order, one app name per line. Delete to reset.\n";
    for (const std::string &name : names) {
        text += name;
        text += '\n';
    }
    return text;
}

std::size_t rank(const std::vector<std::string> &names, std::string_view name)
{
    return static_cast<std::size_t>(std::find(names.begin(), names.end(), name) - names.begin());
}

} // namespace app_user_order
