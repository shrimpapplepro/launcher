/*
 * SPDX-License-Identifier: MIT
 *
 * User-defined home carousel order.
 *
 * The order is stored as one app name per line in
 * $XDG_CONFIG_HOME/APPLaunch/app_order (default ~/.config/APPLaunch/app_order).
 * It is applied on top of the built-in display order: apps listed in the file
 * come first in file order; apps missing from it (newly installed) keep their
 * default relative order after them.
 */
#pragma once

#include <list>
#include <string>
#include <vector>

struct app;

std::string launcher_user_app_order_path();
std::vector<std::string> launcher_load_user_app_order();
bool launcher_save_user_app_order(const std::list<app> &apps);
void launcher_apply_user_app_order(std::list<app> &apps);
