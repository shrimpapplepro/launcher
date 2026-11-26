/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "lvgl/lvgl.h"

#include <list>
#include <string>
#include <vector>

struct app;

/*
 * Applies and persists the user's home carousel order (see
 * model/app_user_order.hpp). The file is read once; moves update the in-memory
 * order and a single write follows a short pause, so a burst of Ctrl+arrow
 * presses costs one SD-card write. LVGL thread only.
 */
class HomeAppOrder
{
public:
    void apply(std::list<app> &apps);
    void remember(const std::list<app> &apps);
    void flush();
    // Writes a pending (timer-delayed) save now; call on shutdown.
    void flush_pending();

private:
    void load();
    static void save_timer_cb(lv_timer_t *timer) noexcept;

    bool loaded_ = false;
    std::vector<std::string> names_;
    lv_timer_t *save_timer_ = nullptr;
};

HomeAppOrder &home_app_order();
