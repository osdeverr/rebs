/**
 * @file re/deps_version_cache.h
 * @author osdever
 * @brief Dependency version caching
 * @version 0.3.0
 * @date 2023-01-14
 * 
 * @copyright Copyright (c) 2023 osdever
 */

#pragma once
#include "target.h"

#include <nlohmann/json.hpp>
#include <functional>

namespace re
{
    /**
     * @brief Represents a dependency version cache for SemVer-pinned dependencies.
     */
    class DepsVersionCache
    {
    public:
        DepsVersionCache() = default;

        /**
         * @brief Construct a new DepsVersionCache object from an existing JSON data node.
         * 
         * @param data The existing JSON to use
         */
        DepsVersionCache(const nlohmann::json& data)
        : mData(data)
        {}

        /**
         * @brief Construct a new DepsVersionCache object from an existing JSON data node (move).
         * 
         * @param data The existing JSON to move from
         */
        DepsVersionCache(nlohmann::json&& data)
        : mData(std::move(data))
        {}

        DepsVersionCache(const DepsVersionCache&) = default;
        DepsVersionCache(DepsVersionCache&&) = default;

        /**
         * @brief Retrieves the latest dependency version that matches the requirements.
         * 
         * @param target The target to which the dependency belongs to
         * @param dep The dependency to match against
         * @param name The dependency's name (in most cases should be set to dep.name)
         * @param get_available_versions A function that returns the available versions for the specified dependency info
         * 
         * @return std::string The most recent version that matches the specified requirements
         */
        ulib::string GetLatestVersionMatchingRequirements(
            const Target& target,
            const TargetDependency& dep,
            ulib::string_view name,
            std::function<std::vector<std::string>(const re::TargetDependency &, const std::string&)> get_available_versions
        );

        /**
         * @brief Returns the JSON state data associated with the cache.
         * This can be saved and later re-loaded into the DepsVersionCache to get the same outputs.
         * 
         * @return The state data
         */
        const nlohmann::json& GetData() const { return mData; }

    private:
        nlohmann::json mData;
    };
}
