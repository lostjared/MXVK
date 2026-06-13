/**
 * @file mxvk_cfg.hpp
 * @brief INI-style configuration file reader/writer.
 *
 * VK_Config parses simple section/key/value configuration files and
 * provides random-access retrieval and modification of settings.
 */
#ifndef _MXVK_CFG_HPP_
#define _MXVK_CFG_HPP_

#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace mxvk {

    /**
     * @class VK_ConfigItem
     * @brief A single key/value pair read from a configuration file.
     */
    class VK_ConfigItem {
      public:
        std::string key;   ///< Configuration key name.
        std::string value; ///< Value associated with the key.
    };

    /**
     * @class VK_Config
     * @brief Parses and manages an INI-style configuration file.
     *
     * Sections are denoted by [SectionName] headers.  Items within a section
     * are stored as key=value pairs.  The file can be loaded, queried,
     * modified in memory, and saved back to disk.
     */
    class VK_Config {
        std::unordered_map<std::string, std::unordered_map<std::string, VK_ConfigItem>> values;
        std::string file_name;

      public:
        /** @brief Default constructor — no file loaded. */
        VK_Config() = default;

        /**
         * @brief Open and parse a configuration file.
         * @param filePath Path to the configuration file.
         */
        explicit VK_Config(const std::string &filePath);

        /** @brief Destructor — closes any open file resources. */
        ~VK_Config();

        VK_Config(const VK_Config &) = delete;
        VK_Config &operator=(const VK_Config &) = delete;
        VK_Config(VK_Config &&) = delete;
        VK_Config &operator=(VK_Config &&) = delete;

        /**
         * @brief Retrieve a configuration item by section and key.
         * @param section Section name (text inside [] brackets).
         * @param key     Key name within that section.
         * @return The matching Item, or a default-constructed one if not found.
         */
        VK_ConfigItem itemAtKey(const std::string &section, const std::string &key) const;

        /**
         * @brief Insert or update a configuration item.
         * @param section Section name.
         * @param key     Key name.
         * @param value   New value string.
         */
        void setItem(const std::string &section, const std::string &key, const std::string &value);

        /**
         * @brief Load (or reload) a configuration file from disk.
         * @param f Path to the file.
         */
        void loadFile(const std::string &f);

        /**
         * @brief Save the current configuration to a file.
         * @param f2 Destination file path.
         */
        void saveFile(const std::string &f2);

        /**
         * @brief Split a string on comma delimiters.
         * @param str Source string.
         * @return Vector of substrings between commas.
         */
        std::vector<std::string> splitByComma(const std::string &str) const;
    };

    // Backward-compatible aliases for older call sites.
    using Item = VK_ConfigItem;
    using ConfigFile = VK_Config;
} // namespace mxvk

#endif