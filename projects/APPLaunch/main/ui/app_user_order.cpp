/*
 * SPDX-License-Identifier: MIT
 */

#include "app_user_order.hpp"

#include "launch.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <system_error>
#include <unordered_map>
#include <unistd.h>

std::string launcher_user_app_order_path()
{
    if (const char *override_path = std::getenv("APPLAUNCH_APP_ORDER_FILE"))
        if (override_path[0] != '\0') return override_path;

    std::string base;
    if (const char *xdg = std::getenv("XDG_CONFIG_HOME"); xdg && xdg[0] == '/') {
        base = xdg;
    } else if (const char *home = std::getenv("HOME"); home && home[0] == '/') {
        base = std::string(home) + "/.config";
    } else {
        return {};
    }
    return base + "/APPLaunch/app_order";
}

std::vector<std::string> launcher_load_user_app_order()
{
    std::vector<std::string> names;
    const std::string path = launcher_user_app_order_path();
    if (path.empty()) return names;

    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() && line[0] != '#') names.push_back(line);
    }
    return names;
}

bool launcher_save_user_app_order(const std::list<app> &apps)
{
    const std::string path = launcher_user_app_order_path();
    if (path.empty()) return false;

    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), error);
    if (error) {
        std::fprintf(stderr, "app_order: cannot create %s: %s\n", path.c_str(),
                     error.message().c_str());
        return false;
    }

    // Write a sibling temp file and rename it so a power cut never leaves a
    // truncated order behind.
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return false;
        out << "# APPLaunch home order, one app name per line. Delete to reset.\n";
        for (const app &item : apps) out << item.Name << '\n';
        out.flush();
        if (!out) return false;
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::perror("app_order: rename");
        ::unlink(tmp.c_str());
        return false;
    }
    return true;
}

void launcher_apply_user_app_order(std::list<app> &apps)
{
    const std::vector<std::string> names = launcher_load_user_app_order();
    if (names.empty()) return;

    std::unordered_map<std::string, int> rank;
    for (std::size_t i = 0; i < names.size(); ++i)
        rank.emplace(names[i], static_cast<int>(i));

    constexpr int unlisted = std::numeric_limits<int>::max();
    // std::list::sort is stable: unlisted apps keep the default order.
    apps.sort([&rank](const app &left, const app &right) {
        const auto l = rank.find(left.Name);
        const auto r = rank.find(right.Name);
        return (l == rank.end() ? unlisted : l->second) <
               (r == rank.end() ? unlisted : r->second);
    });
}
