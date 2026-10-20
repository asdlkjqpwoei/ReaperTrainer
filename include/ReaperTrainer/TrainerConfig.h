#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace ReaperTrainer
{
    class TrainerConfig
    {
    public:
        static TrainerConfig load(const std::filesystem::path& path);

        const std::string& defaultLanguage() const noexcept;
        int defaultMoneyAmount() const noexcept;
        int defaultMoneyAddAmount() const noexcept;
        int defaultSkillChoices() const noexcept;
        int maximumSkillChoices() const noexcept;
        int processScanIntervalMilliseconds() const noexcept;
        int autoCollectIntervalMilliseconds() const noexcept;
        int autoCollectStepPixels() const noexcept;
        int autoCollectRecoveryMilliseconds() const noexcept;
        bool autoCollectEnabledByDefault() const noexcept;
        const std::string& gamePackageFamily() const noexcept;
        const std::string& gameSha256() const noexcept;
        unsigned long long gameModuleSize() const noexcept;
        const std::vector<std::string>& languages() const noexcept;
        const std::string& text(const std::string& language, const std::string& key) const;
        std::string skillName(const std::string& language, int skillId) const;

    private:
        std::string defaultLanguage_;
        std::string gamePackageFamily_;
        std::string gameSha256_;
        int defaultMoneyAmount_{};
        int defaultMoneyAddAmount_{};
        int defaultSkillChoices_{};
        int maximumSkillChoices_{};
        int processScanIntervalMilliseconds_{};
        int autoCollectIntervalMilliseconds_{};
        int autoCollectStepPixels_{};
        int autoCollectRecoveryMilliseconds_{};
        bool autoCollectEnabledByDefault_{};
        unsigned long long gameModuleSize_{};
        std::vector<std::string> languageCodes_;
        std::unordered_map<std::string, std::unordered_map<std::string, std::string>> languages_;
    };
}
