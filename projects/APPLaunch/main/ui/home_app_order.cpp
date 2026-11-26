/*
 * SPDX-License-Identifier: MIT
 */

#include "home_app_order.hpp"

#include "cp0_lvgl_app.h"
#include "hal_lvgl_bsp.h"
#include "launch.h"
#include "model/app_user_order.hpp"
#include "sample_log.h"

#include <cstdio>
#include <unistd.h>

namespace {

std::string order_file_path()
{
    const char *home = cp0_file_path_c("home_dir");
    if (!home || home[0] != '/') return {};
    std::string path = home;
    if (path.back() != '/') path += '/';
    return path + app_user_order::kRelativePath;
}

} // namespace

HomeAppOrder &home_app_order()
{
    static HomeAppOrder order;
    return order;
}

void HomeAppOrder::load()
{
    loaded_ = true;
    const std::string path = order_file_path();
    if (path.empty()) return;
    cp0_signal_filesystem_api(
        {"ReadFile", path, std::to_string(app_user_order::kMaxFileBytes)},
        [this](int code, std::string data) {
            if (code == 0) names_ = app_user_order::parse(data);
        });
}

void HomeAppOrder::apply(std::list<app> &apps)
{
    if (!loaded_) load();
    if (names_.empty()) return;
    // std::list::sort is stable, so unlisted apps keep the default order.
    apps.sort([this](const app &left, const app &right) {
        return app_user_order::rank(names_, left.Name) < app_user_order::rank(names_, right.Name);
    });
}

void HomeAppOrder::remember(const std::list<app> &apps)
{
    names_.clear();
    names_.reserve(apps.size());
    for (const app &item : apps) names_.push_back(item.Name);
    loaded_ = true;

    if (save_timer_) {
        lv_timer_reset(save_timer_);
        return;
    }
    save_timer_ = lv_timer_create(save_timer_cb, app_user_order::kSaveDelayMs, this);
    if (save_timer_) lv_timer_set_repeat_count(save_timer_, 1);
    else flush();
}

void HomeAppOrder::save_timer_cb(lv_timer_t *timer) noexcept
{
    auto *self = static_cast<HomeAppOrder *>(lv_timer_get_user_data(timer));
    if (!self) return;
    self->save_timer_ = nullptr; // a one-shot timer deletes itself after this call
    try {
        self->flush();
    } catch (...) {
    }
}

void HomeAppOrder::flush_pending()
{
    if (!save_timer_) return;
    lv_timer_delete(save_timer_);
    save_timer_ = nullptr;
    flush();
}

void HomeAppOrder::flush()
{
    const std::string path = order_file_path();
    if (path.empty()) return;
    const std::string dir = path.substr(0, path.find_last_of('/'));
    cp0_signal_filesystem_api({"EnsureDirForUser", dir}, [](int, std::string) {});

    // The filesystem signal has no write command; write a temp file and rename
    // it so a power cut never leaves a truncated order behind.
    const std::string text = app_user_order::serialize(names_);
    const std::string tmp = path + ".tmp";
    FILE *file = std::fopen(tmp.c_str(), "w");
    if (!file) {
        SLOGE("[HOME] cannot write %s", tmp.c_str());
        return;
    }
    const bool written = std::fwrite(text.data(), 1, text.size(), file) == text.size();
    if (std::fclose(file) != 0 || !written || std::rename(tmp.c_str(), path.c_str()) != 0) {
        SLOGE("[HOME] cannot save %s", path.c_str());
        ::unlink(tmp.c_str());
    }
}
