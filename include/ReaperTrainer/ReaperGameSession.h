#pragma once

#include "ReaperTrainer/ProcessMemory.h"
#include "ReaperTrainer/TrainerConfig.h"

#include <Windows.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ReaperTrainer
{
    struct SkillInfo
    {
        std::int32_t skillId{};
        std::int32_t skillIndex{};
        std::int32_t currentLevel{};
        std::int32_t maximumLevel{};
    };

    struct ReaperProcessInfo
    {
        DWORD processId{};
        std::uintptr_t moduleBase{};
        std::uint32_t moduleSize{};
        std::wstring imagePath;
        bool writable{};
    };

    struct PlayerPosition
    {
        std::uintptr_t playerAddress{};
        std::uintptr_t transformAddress{};
        std::int32_t xQ16{};
        std::int32_t yQ16{};
    };

    struct PlayerEntityInfo
    {
        std::uintptr_t playerAddress{};
        std::uintptr_t progressAddress{};
        std::uintptr_t transformAddress{};
    };

    struct CoinDropInfo
    {
        std::uintptr_t address{};
        std::int32_t value{};
        std::int32_t spawnTick{};
        std::int32_t gameClock{};
        std::int32_t pickupAgeTicks{};
        bool pickupEligible{};
        std::int32_t remainingBounces{};
        std::int32_t xQ16{};
        std::int32_t yQ16{};
        std::int32_t velocityXQ16{};
        std::int32_t velocityYQ16{};
        std::uintptr_t visualAddress{};
    };

    struct CoinAutoCollectStatus
    {
        std::int32_t activeCoinCount{};
        std::int32_t eligibleCoinCount{};
        std::int32_t movedCoinCount{};
        std::int32_t restoredCoinCount{};
        std::int32_t trackedCoinCount{};
    };

    class ReaperGameSession
    {
    public:
        bool attach(const TrainerConfig& config, std::string& errorKey, DWORD& systemError);
        void detach() noexcept;

        bool isAttached() const noexcept;
        const ReaperProcessInfo& processInfo() const noexcept;

        bool readCurrentPlayer(PlayerEntityInfo& player, std::string& errorKey, DWORD& systemError) const;
        bool readPlayerPosition(PlayerPosition& position, std::string& errorKey, DWORD& systemError) const;
        bool readCoinDrops(std::vector<CoinDropInfo>& drops, std::int32_t& sparkEntityCount,
            std::string& errorKey, DWORD& systemError) const;
        bool moveEligibleCoinDropsTowardPlayer(std::int32_t stepPixels, CoinAutoCollectStatus& status,
            std::string& errorKey, DWORD& systemError);
        bool restoreTrackedCoinDrops(std::int32_t& restoredCoinCount,
            std::string& errorKey, DWORD& systemError);
        bool readPersistentGold(std::int32_t& value, std::string& errorKey, DWORD& systemError) const;
        bool setPersistentGold(std::int32_t value, std::string& errorKey, DWORD& systemError);
        bool addPersistentGold(std::int32_t amount, std::int32_t& result,
            std::string& errorKey, DWORD& systemError);

        bool readSkillChoices(std::int32_t& value, std::string& errorKey, DWORD& systemError) const;
        bool setSkillChoices(std::int32_t value, std::string& errorKey, DWORD& systemError);
        bool addSkillChoices(std::int32_t amount, std::int32_t& result, std::string& errorKey, DWORD& systemError);

        bool readSkills(std::vector<SkillInfo>& skills, std::string& errorKey, DWORD& systemError) const;
        bool setSkillLevel(std::int32_t skillId, std::int32_t level, std::string& errorKey, DWORD& systemError);
        bool replaceSkill(std::int32_t currentSkillId, std::int32_t replacementSkillId,
            std::string& errorKey, DWORD& systemError);

    private:
        struct TrackedCoinDrop
        {
            std::int32_t value{};
            std::int32_t spawnTick{};
            std::uintptr_t visualAddress{};
            std::array<std::int32_t, 4> originalMotion{};
            std::chrono::steady_clock::time_point firstMovedAt;
            bool blockedAfterTimeout{};
        };

        bool readActiveEntities(std::vector<std::uintptr_t>& entities,
            const std::string& unavailableErrorKey, std::string& errorKey, DWORD& systemError) const;
        bool findCurrentPlayer(std::uintptr_t& playerAddress, std::uintptr_t& transformAddress,
            std::string& errorKey, DWORD& systemError) const;
        bool findCurrentProgress(std::uintptr_t& progressAddress, std::string& errorKey, DWORD& systemError) const;
        bool locateMappedValueAddress(std::int32_t index, std::uintptr_t& valueAddress,
            std::int32_t& xorKey, const std::string& unavailableErrorKey,
            std::string& errorKey, DWORD& systemError) const;
        bool readMappedValue(std::int32_t index, std::uintptr_t& valueAddress,
            std::int32_t& xorKey, std::int32_t& storedValue, std::int32_t& value,
            const std::string& unavailableErrorKey, const std::string& invalidErrorKey,
            const std::string& changedErrorKey, std::string& errorKey, DWORD& systemError) const;
        bool locateSkillPointAddress(std::uintptr_t progressAddress, std::uintptr_t& valueAddress,
            std::int32_t& xorKey, std::string& errorKey, DWORD& systemError) const;
        bool writePersistentGold(std::int32_t value, std::int32_t expectedValue,
            std::string& errorKey, DWORD& systemError);
        bool readSkillArray(std::uintptr_t progressAddress, std::uintptr_t& arrayAddress,
            std::int32_t& arrayLength, std::int32_t& xorKey, std::string& errorKey, DWORD& systemError) const;
        bool findSkill(std::int32_t skillId, SkillInfo& skill, std::string& errorKey, DWORD& systemError) const;
        bool writeSkillLevel(const SkillInfo& skill, std::int32_t level,
            std::string& errorKey, DWORD& systemError);

        TrainerConfig config_;
        ReaperProcessInfo processInfo_;
        std::optional<ProcessMemory> memory_;
        std::unordered_map<std::uintptr_t, TrackedCoinDrop> trackedCoinDrops_;
    };
}
