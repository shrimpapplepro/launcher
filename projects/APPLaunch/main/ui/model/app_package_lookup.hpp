/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <string>
#include <string_view>
#include <vector>

/*
 * Finding the Debian package that owns a launcher .desktop file without
 * running dpkg (pure logic, no LVGL or I/O).
 *
 * dpkg records each package's files in /var/lib/dpkg/info/<package>[:arch].list.
 * Packages usually name their .desktop after themselves, so a few guessed list
 * files answer almost every lookup with one small read; only a miss needs the
 * full directory scan.
 */
namespace app_package_lookup {

inline constexpr char kInfoDir[] = "/var/lib/dpkg/info";
// The launcher package itself must never be removed from inside the launcher.
inline constexpr char kLauncherPackage[] = "applaunch";

// Package names to try first for e.g. "perlica_tools.desktop":
// perlica_tools, perlica-tools, perlicatools, m5cardputerzero-perlica_tools, ...
std::vector<std::string> candidate_packages(std::string_view desktop_filename);
// "zeropad.list" -> "zeropad", "foo:arm64.list" -> "foo", anything else -> "".
std::string package_from_list_filename(std::string_view list_filename);
// True when one whole line of a .list file equals path.
bool list_contains_path(std::string_view list_contents, std::string_view path);

} // namespace app_package_lookup
