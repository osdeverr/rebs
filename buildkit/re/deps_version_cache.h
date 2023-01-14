/**
 * @file re/deps_version_cache.h
 * @author Nikita Ivanov
 * @brief Dependency version caching
 * @version 0.3.0
 * @date 2023-01-14
 * 
 * @copyright Copyright (c) 2023 Nikita Ivanov
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
        /**
         * @brief Construct a new DepsVersionCache object from an existing JSON data node.
         * 
         * @param data The existing JSON to use
         */
        DepsVersionCache(const nlohmann::json& data)
        : mData{data}
        {}

        DepsVersionCache(const DepsVersionCache&) = default;
        DepsVersionCache(DepsVersionCache&&) = default;

        /**
         * @brief Retrieves the latest dependency version that matches the requirements.
         * 
         * @param dep The dependency to match against
         * @param get_available_versions A function that returns the available versions for 
         * 
         * @return std::string The most recent version that matches the specified requirements
         */
        std::string GetLatestVersionMatchingRequirements(
            const Target& target,
            const TargetDependency& dep,
            std::function<std::vector<std::string>(const TargetDependency&)> get_available_versions
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
