#include "re_package_client.h"
#include "re/packages/re_package_client.h"

#include <magic_enum.hpp>

#include <nlohmann/json.hpp>

namespace re
{
    std::optional<RePackage> RePackageClient::FindPackage(std::string_view package)
    {
        auto call_url = fmt::format("/api/package/{}", package);
        auto response = mClient.Get(call_url, mHeaders);

        // Throw on response failure
        if (!response)
        {
            RE_THROW RePackageClientException("Failed to find package '{}': HTTP request failed - {}{} [.{}]", package,
                                              mUrl, call_url, magic_enum::enum_name(response.error()));
        }

        // Special case for 404 Not Found: don't throw, just return nullopt
        if (response->status == 404)
        {
            return std::nullopt;
        }

        // Throw on bad status
        if (response->status < 200 || response->status > 299)
        {
            RE_THROW RePackageClientException("Failed to find package '{}': {} {} {}", package, response->status,
                                              response->reason, response->body);
        }

        // We're guaranteed to have a workable JSON object in our response now.
        auto json = nlohmann::json::parse(response->body);

        auto no_case_pred = [](char lhs, char rhs) { return std::tolower(lhs) == std::tolower(rhs); };

        auto type_str = json.at("publishType").get<std::string>();
        auto type = magic_enum::enum_cast<RePackagePublishType>(type_str, no_case_pred);

        if (!type)
        {
            RE_THROW RePackageClientException("Invalid package '{}': unknown package type '{}'", package, type_str);
        }

        RePackage result;

        result.name = json.at("name").get<std::string>();
        result.description = json.at("description").get<std::string>();

        result.author = json.at("author").get<std::string>();
        result.license = json.at("license").get<std::string>();

        result.type = *type;

        if (result.type == RePackagePublishType::External)
        {
            result.hosted_at = json.at("hostedAt").get<std::string>();

            return result;
        }
        else if (result.type == RePackagePublishType::Hosted)
        {
            for (auto &version : json.at("versions"))
            {
                auto tag = version.at("version").get<std::string>();
                result.versions[tag] = RePackageVersion{tag, version.at("link").get<std::string>(), result.name};
            }

            return result;
        }
        else
        {
            RE_THROW RePackageClientException(
                "Invalid package '{}': publish type '{}' is not implemented by this client", package, type_str);
        }
    }

    std::vector<char> RePackageClient::DownloadPackageVersion(const RePackageVersion &version)
    {
        auto response = mClient.Get(version.link, mHeaders);

        // Throw on response failure
        if (!response)
        {
            RE_THROW RePackageClientException("Failed to download package '{}': HTTP request failed - {}{} [.{}]",
                                              version.package, mUrl, version.link,
                                              magic_enum::enum_name(response.error()));
        }

        // Throw on bad status
        if (response->status < 200 || response->status > 299)
        {
            RE_THROW RePackageClientException("Failed to find package '{}': {} {} {}", package, response->status,
                                              response->reason, response->body);
        }
    }
} // namespace re
