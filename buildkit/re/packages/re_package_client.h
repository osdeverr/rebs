#pragma once
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>

#include <re/error.h>

#include <optional>
#include <unordered_map>

namespace re
{
    enum class RePackagePublishType
    {
        External,
        Hosted
    };

    struct RePackageVersion
    {
        std::string version;
        std::string link;
        std::string package;
    };

    struct RePackage
    {
        RePackagePublishType type;

        std::string name;
        std::string description;

        std::string author;
        std::string license;

        std::string version;

        //
        // In case .type == RePackageType::External
        //
        std::string hosted_at;

        //
        // In case .type == RePackageType::Hosted
        //
        std::unordered_map<std::string, RePackageVersion> versions;
    };

    class RePackageClientException : public Exception
    {
    public:
        using Exception::Exception;
    };

    /**
     * @brief Communicates with a Re package server.
     */
    class RePackageClient
    {
    public:
        RePackageClient(const std::string &url) : mClient{url}, mUrl{url}
        {
        }

        const auto &GetUrl() const
        {
            return mUrl;
        }

        std::optional<RePackage> FindPackage(std::string_view package);

        std::vector<char> DownloadPackageVersion(const RePackageVersion &version);

        inline void AddRequestHeader(const std::string &key, const std::string &value)
        {
            mHeaders.insert(std::make_pair(key, value));
        }

    private:
        httplib::Client mClient;
        httplib::Headers mHeaders;

        std::string mUrl;
    };
} // namespace re
