#include "ReaperTrainer/TrainerConfig.h"

#include <charconv>
#include <fstream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace ReaperTrainer
{
    namespace
    {
        std::string trim(std::string value)
        {
            const std::size_t first = value.find_first_not_of(" \r\n");
            if (first == std::string::npos)
            {
                return {};
            }

            const std::size_t last = value.find_last_not_of(" \r\n");
            return value.substr(first, last - first + 1);
        }

        std::string parseScalar(std::string value, std::size_t lineNumber)
        {
            value = trim(std::move(value));
            if (value.empty())
            {
                return {};
            }

            if (value.front() == '"')
            {
                std::string result;
                bool escaped = false;
                std::size_t closingQuote = std::string::npos;
                for (std::size_t index = 1; index < value.size(); ++index)
                {
                    const char current = value[index];
                    if (escaped)
                    {
                        switch (current)
                        {
                        case '"': result.push_back('"'); break;
                        case '\\': result.push_back('\\'); break;
                        case 'n': result.push_back('\n'); break;
                        case 'r': result.push_back('\r'); break;
                        case 't': result.push_back('\t'); break;
                        default:
                            throw std::runtime_error("Unsupported YAML escape at line " + std::to_string(lineNumber));
                        }
                        escaped = false;
                        continue;
                    }

                    if (current == '\\')
                    {
                        escaped = true;
                        continue;
                    }
                    if (current == '"')
                    {
                        closingQuote = index;
                        break;
                    }
                    result.push_back(current);
                }

                if (escaped || closingQuote == std::string::npos)
                {
                    throw std::runtime_error("Unterminated YAML string at line " + std::to_string(lineNumber));
                }

                const std::string suffix = trim(value.substr(closingQuote + 1));
                if (!suffix.empty() && suffix.front() != '#')
                {
                    throw std::runtime_error("Unexpected YAML text at line " + std::to_string(lineNumber));
                }
                return result;
            }

            if (value.front() == '\'')
            {
                std::string result;
                std::size_t closingQuote = std::string::npos;
                for (std::size_t index = 1; index < value.size(); ++index)
                {
                    if (value[index] != '\'')
                    {
                        result.push_back(value[index]);
                        continue;
                    }

                    if (index + 1 < value.size() && value[index + 1] == '\'')
                    {
                        result.push_back('\'');
                        ++index;
                        continue;
                    }

                    closingQuote = index;
                    break;
                }

                if (closingQuote == std::string::npos)
                {
                    throw std::runtime_error("Unterminated YAML string at line " + std::to_string(lineNumber));
                }

                const std::string suffix = trim(value.substr(closingQuote + 1));
                if (!suffix.empty() && suffix.front() != '#')
                {
                    throw std::runtime_error("Unexpected YAML text at line " + std::to_string(lineNumber));
                }
                return result;
            }

            const std::size_t comment = value.find(" #");
            if (comment != std::string::npos)
            {
                value.erase(comment);
            }
            return trim(std::move(value));
        }

        int parseInt(const std::unordered_map<std::string, std::string>& values, const std::string& key)
        {
            const auto entry = values.find(key);
            if (entry == values.end())
            {
                throw std::runtime_error("Missing configuration value: " + key);
            }

            int result{};
            const std::string_view value(entry->second);
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
            {
                throw std::runtime_error("Invalid integer configuration value: " + key);
            }
            return result;
        }

        bool parseBool(const std::unordered_map<std::string, std::string>& values, const std::string& key)
        {
            const auto entry = values.find(key);
            if (entry == values.end())
            {
                throw std::runtime_error("Missing configuration value: " + key);
            }
            if (entry->second == "true")
            {
                return true;
            }
            if (entry->second == "false")
            {
                return false;
            }
            throw std::runtime_error("Invalid boolean configuration value: " + key);
        }

        unsigned long long parseUnsigned(const std::unordered_map<std::string, std::string>& values, const std::string& key)
        {
            const auto entry = values.find(key);
            if (entry == values.end())
            {
                throw std::runtime_error("Missing configuration value: " + key);
            }

            unsigned long long result{};
            const std::string_view value(entry->second);
            int base = 10;
            std::string_view digits = value;
            if (value.size() > 2 && value[0] == '0' && (value[1] == 'x' || value[1] == 'X'))
            {
                base = 16;
                digits.remove_prefix(2);
            }

            const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), result, base);
            if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size())
            {
                throw std::runtime_error("Invalid unsigned configuration value: " + key);
            }
            return result;
        }
    }

    TrainerConfig TrainerConfig::load(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            throw std::runtime_error("Could not open trainer configuration: " + path.string());
        }

        std::unordered_map<std::string, std::string> root;
        std::unordered_map<std::string, std::unordered_map<std::string, std::string>> languages;
        std::vector<std::string> languageCodes;
        bool inLanguages = false;
        std::string currentLanguage;
        std::string line;
        std::size_t lineNumber = 0;

        while (std::getline(input, line))
        {
            ++lineNumber;
            if (lineNumber == 1 && line.size() >= 3 &&
                static_cast<unsigned char>(line[0]) == 0xEF &&
                static_cast<unsigned char>(line[1]) == 0xBB &&
                static_cast<unsigned char>(line[2]) == 0xBF)
            {
                line.erase(0, 3);
            }

            std::size_t indent = 0;
            while (indent < line.size() && line[indent] == ' ')
            {
                ++indent;
            }
            if (indent < line.size() && line[indent] == '\t')
            {
                throw std::runtime_error("Tabs are not allowed for YAML indentation at line " + std::to_string(lineNumber));
            }

            std::string content = trim(line.substr(indent));
            if (content.empty() || content.front() == '#')
            {
                continue;
            }

            const std::size_t colon = content.find(':');
            if (colon == std::string::npos || colon == 0)
            {
                throw std::runtime_error("Expected a YAML key/value entry at line " + std::to_string(lineNumber));
            }

            const std::string key = trim(content.substr(0, colon));
            const std::string scalar = parseScalar(content.substr(colon + 1), lineNumber);
            if (key.empty())
            {
                throw std::runtime_error("Empty YAML key at line " + std::to_string(lineNumber));
            }

            if (indent == 0)
            {
                currentLanguage.clear();
                if (key == "languages")
                {
                    if (!scalar.empty())
                    {
                        throw std::runtime_error("'languages' must be a mapping.");
                    }
                    inLanguages = true;
                    continue;
                }

                inLanguages = false;
                if (!root.emplace(key, scalar).second)
                {
                    throw std::runtime_error("Duplicate YAML key: " + key);
                }
                continue;
            }

            if (indent == 2 && inLanguages)
            {
                if (!scalar.empty())
                {
                    throw std::runtime_error("Language entries must be mappings at line " + std::to_string(lineNumber));
                }
                currentLanguage = key;
                if (!languages.emplace(currentLanguage, std::unordered_map<std::string, std::string>{}).second)
                {
                    throw std::runtime_error("Duplicate language entry: " + currentLanguage);
                }
                languageCodes.push_back(currentLanguage);
                continue;
            }

            if (indent == 4 && inLanguages && !currentLanguage.empty())
            {
                if (!languages[currentLanguage].emplace(key, scalar).second)
                {
                    throw std::runtime_error("Duplicate localized key: " + key);
                }
                continue;
            }

            throw std::runtime_error("Unexpected YAML indentation at line " + std::to_string(lineNumber));
        }

        TrainerConfig config;
        const auto languageEntry = root.find("default_language");
        const auto familyEntry = root.find("game_package_family");
        const auto shaEntry = root.find("game_sha256");
        if (languageEntry == root.end() || familyEntry == root.end() || shaEntry == root.end())
        {
            throw std::runtime_error("The trainer configuration is missing required values.");
        }

        config.defaultLanguage_ = languageEntry->second;
        config.gamePackageFamily_ = familyEntry->second;
        config.gameSha256_ = shaEntry->second;
        config.defaultMoneyAmount_ = parseInt(root, "default_money_amount");
        config.defaultMoneyAddAmount_ = parseInt(root, "default_money_add_amount");
        config.defaultSkillChoices_ = parseInt(root, "default_skill_choices");
        config.maximumSkillChoices_ = parseInt(root, "maximum_skill_choices");
        config.processScanIntervalMilliseconds_ = parseInt(root, "process_scan_interval_ms");
        config.autoCollectIntervalMilliseconds_ = parseInt(root, "auto_collect_interval_ms");
        config.autoCollectStepPixels_ = parseInt(root, "auto_collect_step_pixels");
        config.autoCollectRecoveryMilliseconds_ = parseInt(root, "auto_collect_recovery_ms");
        config.autoCollectEnabledByDefault_ = parseBool(root, "auto_collect_enabled_by_default");
        config.gameModuleSize_ = parseUnsigned(root, "game_module_size");
        config.languageCodes_ = std::move(languageCodes);
        config.languages_ = std::move(languages);

        if (config.defaultMoneyAmount_ < 0 || config.defaultMoneyAddAmount_ <= 0 ||
            config.defaultSkillChoices_ < 0 || config.maximumSkillChoices_ <= 0 ||
            config.defaultSkillChoices_ > config.maximumSkillChoices_ ||
            config.processScanIntervalMilliseconds_ <= 0 ||
            config.autoCollectIntervalMilliseconds_ <= 0 ||
            config.autoCollectStepPixels_ <= 0 ||
            config.autoCollectStepPixels_ > 128 ||
            config.autoCollectRecoveryMilliseconds_ <= 0)
        {
            throw std::runtime_error("A default amount is outside its supported range.");
        }

        if (config.languages_.find(config.defaultLanguage_) == config.languages_.end())
        {
            throw std::runtime_error("The default language is not defined.");
        }

        constexpr const char* requiredKeys[] = {
            "language.name", "app.title", "ui.language", "ui.waiting", "ui.attached",
            "ui.game_disconnected", "ui.waiting_for_player", "ui.player_detected",
            "ui.money_title", "ui.current_money",
            "ui.current_money_value", "ui.money_data_not_ready", "ui.money_data_invalid",
            "ui.money_data_changed", "ui.money_range", "ui.money_updated", "ui.money_added",
            "ui.money_amount", "ui.money_set", "ui.money_add_amount", "ui.money_add",
            "ui.skill_title", "ui.current_skill_choices", "ui.skill_choices_amount",
            "ui.skill_choices_set", "ui.skill_choices_add", "ui.skills_title", "ui.skill_name",
            "ui.skill_level", "ui.skill_max_level", "ui.skill_preview", "ui.skill_replace",
            "ui.skill_learned_placeholder",
            "ui.auto_collect", "ui.auto_collect_help", "ui.ready", "ui.locating",
            "ui.unsupported_game", "ui.access_denied", "ui.write_failed", "ui.invalid_value",
            "ui.no_game", "ui.skills_empty", "ui.skill_id", "ui.error", "ui.help",
            "ui.multiple_games", "ui.process_detection_failed",
            "ui.memory_unavailable", "ui.read_only", "ui.error_code", "ui.money_unavailable",
            "ui.money_save_note", "ui.current_skill_choices_value", "ui.skill_choices_add_amount",
            "ui.skill_choices_updated", "ui.skill_choices_added", "ui.skill_level_value",
            "ui.skill_replacement_placeholder", "ui.skill_replace_help", "ui.skill_replaced",
            "ui.skill_select_first", "ui.skill_not_found", "ui.skill_data_not_ready",
            "ui.skill_data_invalid", "ui.progress_data_not_ready",
            "ui.skill_scan_failed", "ui.skill_choices_range",
            "ui.player_scan_failed", "ui.player_data_not_ready", "ui.player_data_ambiguous",
            "ui.player_position_unstable", "ui.coin_scan_failed",
            "ui.coin_drop_candidates", "ui.spark_entity_candidates",
            "ui.skill_level_range", "ui.skill_same", "ui.skill_not_learned",
            "ui.skill_already_learned", "ui.write_verify_failed", "ui.write_restore_failed",
            "ui.auto_collect_running",
            "ui.auto_collect_disabled", "ui.auto_collect_restored",
            "ui.auto_collect_unavailable", "ui.auto_collect_waiting",
            "ui.config_error"
        };

        for (const std::string& language : config.languageCodes_)
        {
            const auto languageStrings = config.languages_.find(language);
            for (const char* key : requiredKeys)
            {
                if (languageStrings->second.find(key) == languageStrings->second.end())
                {
                    throw std::runtime_error("Language '" + language + "' is missing text key '" + key + "'.");
                }
            }
        }

        return config;
    }

    const std::string& TrainerConfig::defaultLanguage() const noexcept
    {
        return defaultLanguage_;
    }

    int TrainerConfig::defaultMoneyAmount() const noexcept
    {
        return defaultMoneyAmount_;
    }

    int TrainerConfig::defaultMoneyAddAmount() const noexcept
    {
        return defaultMoneyAddAmount_;
    }

    int TrainerConfig::defaultSkillChoices() const noexcept
    {
        return defaultSkillChoices_;
    }

    int TrainerConfig::maximumSkillChoices() const noexcept
    {
        return maximumSkillChoices_;
    }

    int TrainerConfig::processScanIntervalMilliseconds() const noexcept
    {
        return processScanIntervalMilliseconds_;
    }

    int TrainerConfig::autoCollectIntervalMilliseconds() const noexcept
    {
        return autoCollectIntervalMilliseconds_;
    }

    int TrainerConfig::autoCollectStepPixels() const noexcept
    {
        return autoCollectStepPixels_;
    }

    int TrainerConfig::autoCollectRecoveryMilliseconds() const noexcept
    {
        return autoCollectRecoveryMilliseconds_;
    }

    bool TrainerConfig::autoCollectEnabledByDefault() const noexcept
    {
        return autoCollectEnabledByDefault_;
    }

    const std::string& TrainerConfig::gamePackageFamily() const noexcept
    {
        return gamePackageFamily_;
    }

    const std::string& TrainerConfig::gameSha256() const noexcept
    {
        return gameSha256_;
    }

    unsigned long long TrainerConfig::gameModuleSize() const noexcept
    {
        return gameModuleSize_;
    }

    const std::vector<std::string>& TrainerConfig::languages() const noexcept
    {
        return languageCodes_;
    }

    const std::string& TrainerConfig::text(const std::string& language, const std::string& key) const
    {
        const auto languageEntry = languages_.find(language);
        if (languageEntry == languages_.end())
        {
            throw std::out_of_range("Unknown language: " + language);
        }

        const auto textEntry = languageEntry->second.find(key);
        if (textEntry == languageEntry->second.end())
        {
            throw std::out_of_range("Missing localized key: " + key);
        }
        return textEntry->second;
    }

    std::string TrainerConfig::skillName(const std::string& language, int skillId) const
    {
        const auto languageEntry = languages_.find(language);
        if (languageEntry == languages_.end())
        {
            throw std::out_of_range("Unknown language: " + language);
        }

        const auto skillEntry = languageEntry->second.find("skill." + std::to_string(skillId));
        return skillEntry == languageEntry->second.end() ? std::string{} : skillEntry->second;
    }
}
