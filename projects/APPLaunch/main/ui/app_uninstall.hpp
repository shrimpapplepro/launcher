/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "cp0_lvgl_app.h"
#include "lvgl/lvgl.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct app;

/*
 * Uninstall the centred home app from the launcher (Ctrl+Backspace).
 *
 * The app's .desktop file decides what is removed:
 *   - owned by a Debian package -> `dpkg -r <package>`, which refuses when
 *     anything depends on it, so nothing else goes with it
 *   - owned by no package       -> only that launcher entry is deleted
 *   - the launcher's own package, or a built-in page -> refused
 * Root comes from cp0_sudo_run_argv_async and its password prompt.
 *
 * Runs entirely on the LVGL thread. The owner lookup reads the likely dpkg
 * .list file first; only a miss scans the rest, a few files per timer tick,
 * stopping at the first match. Nothing is allocated while the dialog is closed.
 */
class LauncherUninstallFlow
{
public:
    // Opens the dialog; returns false (with a toast) for built-in pages.
    bool open(const app &item);
    bool is_open() const { return overlay_ != nullptr; }
    // Consumes every key while the dialog is open.
    void handle_key(uint32_t code, int key_state);

private:
    enum class State { Lookup, ConfirmPackage, ConfirmEntry, Protected, Running, Failed };

    void build_overlay();
    void close();
    void set_text(const std::string &detail, const std::string &hint, bool error = false);
    bool owner_from_list(const std::string &list_filename);
    void start_scan();
    void scan_step();
    void on_lookup_done();
    void start_removal();
    void on_removal_done(cp0_sudo_result_t result, int exit_code);

    static void scan_timer_cb(lv_timer_t *timer) noexcept;
    static void output_cb(const char *data, size_t size, void *user);
    static void complete_cb(cp0_sudo_result_t result, int exit_code, void *user);

    lv_obj_t *overlay_ = nullptr;
    lv_obj_t *detail_ = nullptr;
    lv_obj_t *hint_ = nullptr;
    lv_timer_t *scan_timer_ = nullptr;
    State state_ = State::Lookup;
    uintptr_t generation_ = 0;
    std::string name_;
    std::string desktop_file_;
    std::string package_;
    std::string output_;
    std::vector<std::string> scan_queue_;
    std::size_t scan_next_ = 0;
};

LauncherUninstallFlow &launcher_uninstall_flow();
