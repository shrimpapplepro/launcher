/*
 * SPDX-License-Identifier: MIT
 */

#include "../main/ui/model/app_package_lookup.hpp"

#include <algorithm>
#include <cassert>
#include <string>
#include <vector>

namespace {

bool contains(const std::vector<std::string> &names, const std::string &name)
{
    return std::find(names.begin(), names.end(), name) != names.end();
}

} // namespace

int main()
{
    using namespace app_package_lookup;

    // Real CP0 packages: zeropad, perlicatools, m5cardputerzero-files.
    assert(candidate_packages("zeropad.desktop").front() == "zeropad");
    assert(contains(candidate_packages("perlica_tools.desktop"), "perlicatools"));
    assert(contains(candidate_packages("perlica_tools.desktop"), "perlica-tools"));
    assert(contains(candidate_packages("files.desktop"), "m5cardputerzero-files"));
    assert(contains(candidate_packages("Foo_Bar.desktop"), "foo-bar"));
    assert(candidate_packages("zeropad.png").empty());
    assert(candidate_packages(".desktop").empty());
    const auto names = candidate_packages("a_b.desktop");
    assert(std::count(names.begin(), names.end(), "ab") == 1);

    assert(package_from_list_filename("zeropad.list") == "zeropad");
    assert(package_from_list_filename("libc6:arm64.list") == "libc6");
    assert(package_from_list_filename("zeropad.md5sums").empty());
    assert(package_from_list_filename(".list").empty());

    const std::string list = "/.\n/usr/share/APPLaunch/applications/zeropad.desktop\n/usr/bin/zeropad\n";
    assert(list_contains_path(list, "/usr/share/APPLaunch/applications/zeropad.desktop"));
    assert(list_contains_path(list, "/usr/bin/zeropad"));
    assert(!list_contains_path(list, "/usr/bin/zero"));           // prefix of a line
    assert(!list_contains_path(list, "applications/zeropad.desktop")); // suffix of a line
    assert(!list_contains_path(list, ""));
    return 0;
}
