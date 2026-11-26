/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

/*
 * User-defined home carousel order (pure logic, no LVGL or I/O).
 *
 * Stored as one app name per line in ~/.config/APPLaunch/app_order. Apps named
 * in the file sort first, in file order; apps missing from it (newly
 * installed) keep the default order after them. Delete the file to reset.
 */
namespace app_user_order {

inline constexpr char kRelativePath[] = ".config/APPLaunch/app_order";
// One name per app; a few dozen short names. Anything larger is not ours.
inline constexpr std::size_t kMaxFileBytes = 8 * 1024;
// Coalesce a burst of moves into one write.
inline constexpr unsigned kSaveDelayMs = 1500;

// Parses the file: skips blank and '#' lines, strips CR, drops duplicates.
std::vector<std::string> parse(std::string_view text);
std::string serialize(const std::vector<std::string> &names);
// Position of name in names, or names.size() when absent.
std::size_t rank(const std::vector<std::string> &names, std::string_view name);

} // namespace app_user_order
