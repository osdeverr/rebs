#include <fmt/format.h>
#include <nlohmann/json.hpp>

int main()
{
    nlohmann::json json;

    json["hello"] = "world";
    json["message"] = fmt::format("The answer to everything is {}", 42);

    fmt::print("{}\n", json.dump(4));
}
