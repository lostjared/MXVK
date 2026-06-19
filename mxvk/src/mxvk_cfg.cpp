/**
 * @file mxvk_cfg.cpp
 * @brief Implementation of INI-style config file reader/writer (mxvk::VK_Config).
 */
#include "mxvk/mxvk_cfg.hpp"

#include <algorithm>
#include <format>
#include <ranges>

#include "mxvk/mxvk_exception.hpp"

namespace mxvk {
    void VK_Config::loadFile(const std::string &f) {
        std::ifstream in(f);
        if (!in.is_open()) {
            std::ofstream out(f);
            if (!out.is_open()) {
                throw mxvk::Exception(std::format("mxvk: Could not create configuration file: {}", f));
            }

            values.clear();
            return;
        }

        values.clear();

        std::string line, currentSection;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == ';' || line[0] == '#') {
                continue;
            }

            if (line.front() == '[' && line.back() == ']') {
                currentSection = line.substr(1, line.size() - 2);
                continue;
            }

            size_t pos = line.find('=');
            if (pos != std::string::npos && !currentSection.empty()) {
                std::string key = line.substr(0, pos);
                std::string value = line.substr(pos + 1);

                VK_ConfigItem item;
                item.key = key;
                item.value = value;

                values[currentSection][key] = item;
            }
        }

        in.close();
    }
    void VK_Config::saveFile(const std::string &f2) {
        std::ofstream out(f2);
        if (!out.is_open()) {
            return;
        }

        std::vector<std::string> sectionNames;
        for (const auto &section : values) {
            sectionNames.push_back(section.first);
        }
        std::ranges::sort(sectionNames);

        for (const auto &section : sectionNames) {
            out << "[" << section << "]\n";

            std::vector<std::string> keys;
            for (const auto &pair : values[section]) {
                keys.push_back(pair.first);
            }
            std::ranges::sort(keys);

            for (const auto &key : keys) {
                out << key << "=" << values[section][key].value << "\n";
            }

            out << "\n";
        }

        out.close();
    }

    VK_ConfigItem VK_Config::itemAtKey(const std::string &section, const std::string &key, const std::string &default_value) const {
        const auto sectionIt = values.find(section);
        if (sectionIt == values.end()) {
            return {key, default_value};
        }
        const auto keyIt = sectionIt->second.find(key);
        if (keyIt == sectionIt->second.end()) {
            throw mxvk::Exception(std::format("mxvk: Could not find key '{}' in section '{}'.", key, section));
        }
        return keyIt->second;
    }

    void VK_Config::setItem(const std::string &section, const std::string &key, const std::string &value) {
        values[section][key] = {key, value};
    }

    std::vector<std::string> VK_Config::splitByComma(const std::string &str) const {
        std::vector<std::string> result;
        size_t start = 0, end = 0;

        while ((end = str.find(',', start)) != std::string::npos) {
            result.push_back(str.substr(start, end - start));
            start = end + 1;
        }

        result.push_back(str.substr(start));

        return result;
    }

    VK_Config::VK_Config(const std::string &filePath) : file_name(filePath) {
        loadFile(filePath);
    }

    VK_Config::~VK_Config() {
        if (!file_name.empty()) {
            saveFile(file_name);
        }
    }

} // namespace mxvk
