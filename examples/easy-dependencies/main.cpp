#include <fmt/format.h>
#include <nlohmann/json.hpp>

int main()
{
    nlohmann::json json = {
        {"foo", "bar"},
        {"how_awesome_re_is_from_0_to_10", 12}
    };

    fmt::print("{}\n", json.dump(4));
}
