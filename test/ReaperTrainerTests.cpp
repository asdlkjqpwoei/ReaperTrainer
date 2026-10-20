#include "ReaperTrainer/TrainerConfig.h"

#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "Expected a trainer.yaml path.\n";
        return 2;
    }

    try
    {
        const ReaperTrainer::TrainerConfig config =
            ReaperTrainer::TrainerConfig::load(std::filesystem::path(argv[1]));

        if (config.defaultLanguage() != "zh-CN" ||
            config.defaultMoneyAmount() != 10000 ||
            config.defaultMoneyAddAmount() != 100 ||
            config.defaultSkillChoices() != 1 ||
            config.maximumSkillChoices() != 99 ||
            config.processScanIntervalMilliseconds() != 1000 ||
            config.autoCollectIntervalMilliseconds() != 50 ||
            config.autoCollectStepPixels() != 30 ||
            config.autoCollectRecoveryMilliseconds() != 5000 ||
            !config.autoCollectEnabledByDefault() ||
            config.gameModuleSize() != 0x228000ULL ||
            config.languages().size() != 2)
        {
            throw std::runtime_error("YAML configuration values do not match the expected defaults.");
        }

        if (config.text("zh-CN", "app.title") == config.text("en", "app.title"))
        {
            throw std::runtime_error("Localized titles should differ.");
        }
        if (config.text("zh-CN", "ui.money_title") != "金币修改" ||
            config.text("en", "ui.money_title") != "Gold editor" ||
        config.text("zh-CN", "ui.waiting_for_player").find("{0}") == std::string::npos ||
        config.text("en", "ui.player_detected").empty() ||
            config.text("zh-CN", "ui.current_money_value").find("{0}") == std::string::npos ||
            config.text("en", "ui.current_money_value").find("{0}") == std::string::npos ||
            config.text("zh-CN", "ui.progress_data_not_ready").empty() ||
            config.text("en", "ui.progress_data_not_ready").empty() ||
            config.text("zh-CN", "ui.money_data_not_ready").empty() ||
            config.text("en", "ui.money_data_invalid").empty() ||
            config.text("zh-CN", "ui.money_updated").empty() ||
            config.text("en", "ui.money_added").empty() ||
            config.text("zh-CN", "ui.money_save_note").empty() ||
            config.text("en", "ui.money_save_note").empty() ||
            config.text("zh-CN", "ui.skill_learned_placeholder").empty() ||
            config.text("en", "ui.skill_learned_placeholder").empty() ||
            config.text("zh-CN", "ui.coin_drop_candidates").find("{0}") == std::string::npos ||
            config.text("en", "ui.spark_entity_candidates").find("{0}") == std::string::npos ||
            config.text("zh-CN", "ui.auto_collect_running").find("{4}") == std::string::npos ||
            config.text("en", "ui.auto_collect_running").find("{4}") == std::string::npos ||
            config.text("zh-CN", "ui.auto_collect_help").find("{1}") == std::string::npos ||
            config.text("en", "ui.auto_collect_help").find("{0}") == std::string::npos ||
            config.text("zh-CN", "ui.auto_collect_help").find("{2}") == std::string::npos ||
            config.text("en", "ui.auto_collect_help").find("{2}") == std::string::npos ||
            config.text("zh-CN", "ui.auto_collect_unavailable").empty() ||
            config.text("zh-CN", "ui.auto_collect_waiting").empty() ||
            config.text("en", "ui.auto_collect_waiting").empty() ||
            config.text("en", "ui.auto_collect_restored").empty())
        {
            throw std::runtime_error("Localized trainer text is missing or malformed.");
        }

        constexpr std::array<int, 30> gameSkillIds{
            1325, 1326, 1347, 1348, 1328, 1334, 1335, 1340, 1351, 1341,
            1331, 1332, 1343, 1329, 1352, 1342, 1344, 1350, 1353, 1339,
            1349, 1330, 1336, 1337, 1327, 1354, 1333, 1346, 1338, 1345
        };
        for (const int skillId : gameSkillIds)
        {
            if (config.skillName("zh-CN", skillId).empty() ||
                config.skillName("en", skillId).empty())
            {
                throw std::runtime_error("A supported skill is missing its configured name.");
            }
        }
        if (config.skillName("zh-CN", 1331) != "Mighty Slam" ||
            config.skillName("en", 1352) != "Concussion" ||
            !config.skillName("en", 0).empty())
        {
            throw std::runtime_error("Skill name lookup returned an unexpected result.");
        }

        std::cout << "All ReaperTrainer module checks passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
