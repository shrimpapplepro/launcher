/*
 * SPDX-License-Identifier: MIT
 */

#include "../main/ui/model/app_user_order.hpp"

#include <cassert>
#include <string>
#include <vector>

int main()
{
    using namespace app_user_order;

    const std::vector<std::string> names = parse("# header\r\nSettings\r\n\nCLI\nSettings\nIP Panel");
    assert((names == std::vector<std::string>{"Settings", "CLI", "IP Panel"}));

    assert(rank(names, "CLI") == 1);
    assert(rank(names, "Unknown") == names.size());

    // serialize -> parse round-trips and keeps names with spaces intact.
    assert(parse(serialize(names)) == names);
    assert(parse("").empty());
    assert(parse("\n\n# only comments\n").empty());
    return 0;
}
