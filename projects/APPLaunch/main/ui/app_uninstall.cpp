/*
 * SPDX-License-Identifier: MIT
 */

#include "app_uninstall.hpp"

#include "hal_lvgl_bsp.h"
#include "input_keys.h"
#include "launch.h"
#include "launcher_platform.hpp"
#include "launcher_toast.h"
#include "model/app_package_lookup.hpp"
#include "ui.h"

#include <sstream>

namespace {

namespace lookup = app_package_lookup;

constexpr uint32_t kPanelColor = 0x1E1E1E;
constexpr uint32_t kBorderColor = 0x4A4A4A;
constexpr uint32_t kTextColor = 0xFFFFFF;
constexpr uint32_t kMutedColor = 0xA8A8A8;
constexpr uint32_t kErrorColor = 0xFF6B6B;
// Fallback scan pacing: small batches keep the UI responsive.
constexpr uint32_t kScanPeriodMs = 10;
constexpr std::size_t kScanFilesPerTick = 8;
// Large enough for any package's file list (kernel images list ~4k paths).
constexpr char kMaxListBytes[] = "4194304";
constexpr std::size_t kMaxOutputBytes = 4096;

const lv_font_t *ui_font(int size, bool bold)
{
    return launcher_fonts().get("AlibabaPuHuiTi-3-55-Regular.ttf", size,
                                bold ? LV_FREETYPE_FONT_STYLE_BOLD : LV_FREETYPE_FONT_STYLE_NORMAL);
}

std::string read_file(const std::string &path)
{
    std::string contents;
    cp0_signal_filesystem_api({"ReadFile", path, kMaxListBytes}, [&](int code, std::string data) {
        if (code == 0) contents = std::move(data);
    });
    return contents;
}

std::string basename_of(const std::string &path)
{
    const std::size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string last_line(std::string text)
{
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' '))
        text.pop_back();
    const std::size_t newline = text.find_last_of('\n');
    if (newline != std::string::npos) text.erase(0, newline + 1);
    if (text.size() > 120) text = text.substr(0, 117) + "...";
    return text;
}

} // namespace

LauncherUninstallFlow &launcher_uninstall_flow()
{
    static LauncherUninstallFlow flow;
    return flow;
}

bool LauncherUninstallFlow::open(const app &item)
{
    if (overlay_) return true;
    if (item.DesktopFile.empty()) {
        launcher_toast().show("Built-in apps can't be uninstalled");
        return false;
    }

    name_ = item.Name;
    desktop_file_ = item.DesktopFile;
    package_.clear();
    state_ = State::Lookup;
    build_overlay();

    for (const std::string &candidate : lookup::candidate_packages(basename_of(desktop_file_))) {
        if (owner_from_list(candidate + ".list") || owner_from_list(candidate + ":arm64.list")) {
            on_lookup_done();
            return true;
        }
    }
    start_scan();
    return true;
}

bool LauncherUninstallFlow::owner_from_list(const std::string &list_filename)
{
    const std::string contents = read_file(std::string(lookup::kInfoDir) + "/" + list_filename);
    if (!lookup::list_contains_path(contents, desktop_file_)) return false;
    package_ = lookup::package_from_list_filename(list_filename);
    return !package_.empty();
}

void LauncherUninstallFlow::start_scan()
{
    set_text("Checking which package owns it...", "Esc  Cancel");
    scan_queue_.clear();
    scan_next_ = 0;
    std::string listing;
    cp0_signal_filesystem_api({"DirList", lookup::kInfoDir}, [&](int code, std::string data) {
        if (code == 0) listing = std::move(data);
    });
    std::istringstream lines(listing);
    std::string line;
    std::string name;
    while (std::getline(lines, line)) {
        if (line.size() < 3 || line[0] != 'F' || line[1] != '\t') continue;
        if (launcher_platform::decode_field(line.substr(2), name) &&
            !lookup::package_from_list_filename(name).empty())
            scan_queue_.push_back(name);
    }
    scan_timer_ = lv_timer_create(scan_timer_cb, kScanPeriodMs, this);
    if (!scan_timer_) on_lookup_done();
}

void LauncherUninstallFlow::scan_timer_cb(lv_timer_t *timer) noexcept
{
    try {
        if (auto *self = static_cast<LauncherUninstallFlow *>(lv_timer_get_user_data(timer)))
            self->scan_step();
    } catch (...) {
    }
}

void LauncherUninstallFlow::scan_step()
{
    for (std::size_t n = 0; n < kScanFilesPerTick && scan_next_ < scan_queue_.size(); ++n) {
        if (owner_from_list(scan_queue_[scan_next_++])) break;
    }
    if (package_.empty() && scan_next_ < scan_queue_.size()) return;

    lv_timer_delete(scan_timer_);
    scan_timer_ = nullptr;
    std::vector<std::string>().swap(scan_queue_);
    on_lookup_done();
}

void LauncherUninstallFlow::on_lookup_done()
{
    if (package_.empty()) {
        state_ = State::ConfirmEntry;
        set_text("Not from a package. Removes only the launcher entry:\n" + desktop_file_,
                 "Enter  Remove      Esc  Cancel");
    } else if (package_ == lookup::kLauncherPackage) {
        state_ = State::Protected;
        set_text("Part of the launcher itself, can't be removed here.", "Esc  Close");
    } else {
        state_ = State::ConfirmPackage;
        set_text("Removes the package " + package_ + " (dpkg -r).\nAsks for your password.",
                 "Enter  Uninstall      Esc  Cancel");
    }
}

void LauncherUninstallFlow::handle_key(uint32_t code, int key_state)
{
    if (key_state != 0) return; // act on release so the press can't leak elsewhere
    if (code == KEY_ESC && state_ != State::Running) {
        close();
    } else if (code == KEY_ENTER &&
               (state_ == State::ConfirmPackage || state_ == State::ConfirmEntry)) {
        start_removal();
    }
}

void LauncherUninstallFlow::start_removal()
{
    state_ = State::Running;
    output_.clear();
    set_text(package_.empty() ? "Removing entry..." : "Uninstalling " + package_ + "...", "");

    const char *dpkg_argv[] = {"/usr/bin/dpkg", "-r", package_.c_str(), nullptr};
    const char *rm_argv[] = {"/bin/rm", "-f", "--", desktop_file_.c_str(), nullptr};
    const int rc = cp0_sudo_run_argv_async(package_.empty() ? rm_argv : dpkg_argv,
                                           CP0_SUDO_CALLBACK_LVGL, output_cb, complete_cb,
                                           reinterpret_cast<void *>(generation_));
    if (rc < 0) on_removal_done(CP0_SUDO_RESULT_EXEC_FAILED, rc);
}

void LauncherUninstallFlow::output_cb(const char *data, size_t size, void *user)
{
    LauncherUninstallFlow &flow = launcher_uninstall_flow();
    if (reinterpret_cast<uintptr_t>(user) != flow.generation_ || !data) return;
    // Only the tail is shown; keep a bounded window of it.
    flow.output_.append(data, size);
    if (flow.output_.size() > kMaxOutputBytes)
        flow.output_.erase(0, flow.output_.size() - kMaxOutputBytes);
}

void LauncherUninstallFlow::complete_cb(cp0_sudo_result_t result, int exit_code, void *user)
{
    LauncherUninstallFlow &flow = launcher_uninstall_flow();
    if (reinterpret_cast<uintptr_t>(user) == flow.generation_)
        flow.on_removal_done(result, exit_code);
}

void LauncherUninstallFlow::on_removal_done(cp0_sudo_result_t result, int exit_code)
{
    if (!overlay_) return;
    if (result == CP0_SUDO_RESULT_SUCCESS && exit_code == 0) {
        const std::string message = "Uninstalled " + name_;
        close();
        // The applications directory watcher drops the app from the carousel.
        launcher_toast().show(message.c_str());
        return;
    }
    if (result == CP0_SUDO_RESULT_CANCELLED) {
        close();
        return;
    }
    state_ = State::Failed;
    std::string reason = last_line(output_);
    if (result == CP0_SUDO_RESULT_AUTH_FAILED) reason = "Wrong password.";
    else if (result == CP0_SUDO_RESULT_TIMED_OUT) reason = "Timed out.";
    else if (reason.empty()) reason = "Failed (exit " + std::to_string(exit_code) + ").";
    output_.clear();
    set_text(reason, "Esc  Close", true);
}

void LauncherUninstallFlow::build_overlay()
{
    overlay_ = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(overlay_);
    lv_obj_set_size(overlay_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay_, LV_OPA_60, 0);
    lv_obj_clear_flag(overlay_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *panel = lv_obj_create(overlay_);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, 296, LV_SIZE_CONTENT);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, lv_color_hex(kPanelColor), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(kBorderColor), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, 10, 0);
    lv_obj_set_style_pad_all(panel, 10, 0);
    lv_obj_set_style_pad_row(panel, 6, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(panel);
    lv_obj_set_width(title, LV_PCT(100));
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(title, ui_font(16, true), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(kTextColor), 0);
    lv_label_set_text(title, ("Uninstall " + name_ + "?").c_str());

    detail_ = lv_label_create(panel);
    lv_obj_set_width(detail_, LV_PCT(100));
    lv_label_set_long_mode(detail_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(detail_, ui_font(12, false), 0);

    hint_ = lv_label_create(panel);
    lv_obj_set_width(hint_, LV_PCT(100));
    lv_obj_set_style_text_font(hint_, ui_font(12, true), 0);
    lv_obj_set_style_text_color(hint_, lv_color_hex(kTextColor), 0);
    lv_obj_set_style_text_align(hint_, LV_TEXT_ALIGN_RIGHT, 0);
}

void LauncherUninstallFlow::set_text(const std::string &detail, const std::string &hint, bool error)
{
    if (!overlay_) return;
    lv_obj_set_style_text_color(detail_, lv_color_hex(error ? kErrorColor : kMutedColor), 0);
    lv_label_set_text(detail_, detail.c_str());
    lv_label_set_text(hint_, hint.c_str());
    if (hint.empty()) lv_obj_add_flag(hint_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(hint_, LV_OBJ_FLAG_HIDDEN);
}

void LauncherUninstallFlow::close()
{
    ++generation_; // late sudo callbacks for this dialog are ignored
    if (scan_timer_) lv_timer_delete(scan_timer_);
    scan_timer_ = nullptr;
    std::vector<std::string>().swap(scan_queue_);
    if (overlay_) lv_obj_delete(overlay_);
    overlay_ = detail_ = hint_ = nullptr;
    state_ = State::Lookup;
}
