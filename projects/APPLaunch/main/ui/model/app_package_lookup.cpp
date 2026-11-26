/*
 * SPDX-License-Identifier: MIT
 */

#include "app_package_lookup.hpp"

#include <algorithm>

namespace app_package_lookup {

namespace {

void add_unique(std::vector<std::string> &names, std::string name)
{
    if (!name.empty() && std::find(names.begin(), names.end(), name) == names.end())
        names.push_back(std::move(name));
}

} // namespace

std::vector<std::string> candidate_packages(std::string_view desktop_filename)
{
    std::vector<std::string> names;
    constexpr std::string_view suffix = ".desktop";
    if (desktop_filename.size() <= suffix.size() ||
        desktop_filename.substr(desktop_filename.size() - suffix.size()) != suffix)
        return names;

    const std::string stem(desktop_filename.substr(0, desktop_filename.size() - suffix.size()));
    std::string dashed = stem;
    std::replace(dashed.begin(), dashed.end(), '_', '-');
    std::string joined = stem;
    joined.erase(std::remove(joined.begin(), joined.end(), '_'), joined.end());

    for (const std::string &base : {stem, dashed, joined}) {
        std::string lower = base;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c); });
        add_unique(names, lower);
        add_unique(names, "m5cardputerzero-" + lower); // M5Stack's product packages
    }
    return names;
}

std::string package_from_list_filename(std::string_view list_filename)
{
    constexpr std::string_view suffix = ".list";
    if (list_filename.size() <= suffix.size() ||
        list_filename.substr(list_filename.size() - suffix.size()) != suffix)
        return {};
    std::string_view package = list_filename.substr(0, list_filename.size() - suffix.size());
    const std::size_t colon = package.find(':'); // multiarch "name:arch"
    if (colon != std::string_view::npos) package = package.substr(0, colon);
    return std::string(package);
}

bool list_contains_path(std::string_view list_contents, std::string_view path)
{
    if (path.empty()) return false;
    std::size_t at = 0;
    while ((at = list_contents.find(path, at)) != std::string_view::npos) {
        const bool line_start = at == 0 || list_contents[at - 1] == '\n';
        const std::size_t after = at + path.size();
        const bool line_end = after == list_contents.size() || list_contents[after] == '\n';
        if (line_start && line_end) return true;
        at = after;
    }
    return false;
}

} // namespace app_package_lookup
