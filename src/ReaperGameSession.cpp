#include "ReaperTrainer/ReaperGameSession.h"

#include <Windows.h>
#include <appmodel.h>
#include <bcrypt.h>
#include <TlHelp32.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace ReaperTrainer
{
    namespace
    {
        constexpr std::uintptr_t levelupSelectorGlobalRva = 0x20F6F0;
        constexpr std::uintptr_t levelupSelectorVtableRva = 0x1FBD58;
        constexpr std::uintptr_t campaignVtableRva = 0x1FE538;
        constexpr std::uintptr_t campaignProgressOffset = 0x250;
        constexpr std::uintptr_t gameGlobalRva = 0x20F690;
        constexpr std::uintptr_t gameClockRva = 0x20E7EC;
        constexpr std::uintptr_t gameVtableRva = 0x1FE168;
        constexpr std::uintptr_t gameEntityManagerOffset = 0xA8;
        constexpr std::uintptr_t entityManagerVtableRva = 0x1FB8C8;
        constexpr std::uintptr_t entityCountsOffset = 0x40;
        constexpr std::uintptr_t entityCountsVtableRva = 0x1F3E90;
        constexpr std::uintptr_t entityBucketsOffset = 0x48;
        constexpr std::uintptr_t entityBucketsVtableRva = 0x1FEFF0;
        constexpr std::uintptr_t entityBucketVtableRva = 0x1FEFD8;
        constexpr std::uintptr_t entityBucketDataOffset = 0x18;
        constexpr std::uintptr_t entityBucketCapacityOffset = 0x20;
        constexpr std::uintptr_t playerVtableRva = 0x1FB900;
        constexpr std::uintptr_t currentPlayerGlobalRva = 0x20F680;
        constexpr std::uintptr_t worldPlayerGlobalRva = 0x20F740;
        constexpr std::uintptr_t playerProgressVtableRva = 0x1F9798;
        constexpr std::uintptr_t mainGlobalRva = 0x20F018;
        constexpr std::uintptr_t mainVtableRva = 0x1F4B98;
        constexpr std::uintptr_t mainCampaignOffset = 0x20;
        constexpr std::uintptr_t persistentGoldIndexOffset = 0x1C;
        constexpr std::uintptr_t playerTransformOffset = 0x98;
        constexpr std::uintptr_t transformVtableRva = 0x1F9218;
        constexpr std::uintptr_t transformPositionXOffset = 0x40;
        constexpr std::uintptr_t transformPositionYOffset = 0x44;
        constexpr std::uintptr_t lootItemVtableRva = 0x1FBF10;
        constexpr std::uintptr_t lootValueOffset = 0x74;
        constexpr std::uintptr_t lootSpawnTickOffset = 0x50;
        constexpr std::uintptr_t lootBounceCountOffset = 0x80;
        constexpr std::uintptr_t lootPositionXOffset = 0x90;
        constexpr std::uintptr_t lootPositionYOffset = 0x94;
        constexpr std::uintptr_t lootVelocityXOffset = 0x98;
        constexpr std::uintptr_t lootVelocityYOffset = 0x9C;
        constexpr std::uintptr_t lootVisualOffset = 0xA0;
        constexpr std::uintptr_t sparkEntityVtableRva = 0x1FEED8;
        constexpr std::uintptr_t skillVtableRva = 0x1FEBB0;
        constexpr std::uintptr_t pointIndexMapGlobalRva = 0x20F780;
        constexpr std::uintptr_t pointValuesGlobalRva = 0x20F778;
        constexpr std::uintptr_t pointXorKeyRva = 0x20EA5C;
        constexpr std::uintptr_t vectorDataOffset = 0x18;
        constexpr std::uintptr_t vectorLengthOffset = 0x20;
        constexpr std::size_t maximumEntityBucketCount = 64;
        constexpr std::size_t maximumEntityBucketCapacity = 4096;
        constexpr std::size_t maximumActiveEntityCount = 8192;
        constexpr std::size_t maximumSkillInstances = 64;
        constexpr std::int32_t maximumSkillArrayLength = 512;

        std::wstring utf8ToWide(const std::string& value)
        {
            if (value.empty())
            {
                return {};
            }

            const int required = MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                value.data(),
                static_cast<int>(value.size()),
                nullptr,
                0);
            if (required <= 0)
            {
                throw std::runtime_error("Invalid UTF-8 configuration value.");
            }

            std::wstring result(static_cast<std::size_t>(required), L'\0');
            if (MultiByteToWideChar(
                    CP_UTF8,
                    MB_ERR_INVALID_CHARS,
                    value.data(),
                    static_cast<int>(value.size()),
                    result.data(),
                    required) != required)
            {
                throw std::runtime_error("Could not convert a UTF-8 configuration value.");
            }
            return result;
        }

        std::wstring lowercase(std::wstring value)
        {
            std::transform(value.begin(), value.end(), value.begin(),
                [](wchar_t character) { return static_cast<wchar_t>(towlower(character)); });
            return value;
        }

        bool readPackageFamilyName(
            HANDLE processHandle,
            std::wstring& packageFamilyName,
            DWORD& systemError)
        {
            packageFamilyName.clear();
            systemError = ERROR_SUCCESS;

            UINT32 requiredLength = 0;
            LONG result = GetPackageFamilyName(processHandle, &requiredLength, nullptr);
            if ((result != ERROR_INSUFFICIENT_BUFFER && result != ERROR_SUCCESS) ||
                requiredLength == 0)
            {
                systemError = static_cast<DWORD>(result);
                return false;
            }

            std::wstring resultBuffer(requiredLength, L'\0');
            result = GetPackageFamilyName(processHandle, &requiredLength, resultBuffer.data());
            if (result != ERROR_SUCCESS)
            {
                systemError = static_cast<DWORD>(result);
                return false;
            }

            const std::size_t nameLength = wcsnlen_s(resultBuffer.c_str(), resultBuffer.size());
            if (nameLength == 0)
            {
                systemError = ERROR_INVALID_DATA;
                return false;
            }

            resultBuffer.resize(nameLength);
            packageFamilyName = std::move(resultBuffer);
            return true;
        }

        bool readRemoteValue(
            const ProcessMemory& memory,
            std::uintptr_t address,
            std::uintptr_t& value,
            DWORD& systemError)
        {
            return memory.readValue(address, value, systemError);
        }

        bool calculateSha256(const std::wstring& path, std::string& hash)
        {
            BCRYPT_ALG_HANDLE algorithm{};
            BCRYPT_HASH_HANDLE hashHandle{};
            std::vector<UCHAR> hashObject;
            std::vector<UCHAR> hashBytes;

            if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
            {
                return false;
            }

            ULONG objectLength = 0;
            ULONG hashLength = 0;
            ULONG resultLength = 0;
            bool succeeded =
                BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                    reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength),
                    &resultLength, 0) >= 0 &&
                BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
                    reinterpret_cast<PUCHAR>(&hashLength), sizeof(hashLength),
                    &resultLength, 0) >= 0;

            if (succeeded)
            {
                hashObject.resize(objectLength);
                hashBytes.resize(hashLength);
                succeeded = BCryptCreateHash(
                    algorithm,
                    &hashHandle,
                    hashObject.data(),
                    objectLength,
                    nullptr,
                    0,
                    0) >= 0;
            }

            if (succeeded)
            {
                std::ifstream input(std::filesystem::path(path), std::ios::binary);
                if (!input)
                {
                    succeeded = false;
                }
                else
                {
                    std::array<char, 64 * 1024> buffer{};
                    while (input)
                    {
                        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                        const std::streamsize bytesRead = input.gcount();
                        if (bytesRead > 0 &&
                            BCryptHashData(
                                hashHandle,
                                reinterpret_cast<PUCHAR>(buffer.data()),
                                static_cast<ULONG>(bytesRead),
                                0) < 0)
                        {
                            succeeded = false;
                            break;
                        }
                    }
                    if (input.bad())
                    {
                        succeeded = false;
                    }
                }
            }

            if (succeeded)
            {
                succeeded = BCryptFinishHash(hashHandle, hashBytes.data(), hashLength, 0) >= 0;
            }

            if (hashHandle != nullptr)
            {
                BCryptDestroyHash(hashHandle);
            }
            BCryptCloseAlgorithmProvider(algorithm, 0);

            if (!succeeded)
            {
                return false;
            }

            std::ostringstream formatted;
            formatted << std::uppercase << std::hex << std::setfill('0');
            for (const UCHAR byte : hashBytes)
            {
                formatted << std::setw(2) << static_cast<unsigned int>(byte);
            }
            hash = formatted.str();
            return true;
        }

        bool findInstalledReaper(
            const TrainerConfig& config,
            ReaperProcessInfo& processInfo,
            std::string& errorKey,
            DWORD& systemError)
        {
            const std::wstring expectedFamily = utf8ToWide(config.gamePackageFamily());
            const std::wstring expectedExecutable = L"Reaper.exe";
            const std::string expectedHash = config.gameSha256();
            std::vector<ReaperProcessInfo> matches;

            HANDLE processSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (processSnapshot == INVALID_HANDLE_VALUE)
            {
                systemError = GetLastError();
                errorKey = "ui.process_detection_failed";
                return false;
            }

            PROCESSENTRY32W processEntry{};
            processEntry.dwSize = sizeof(processEntry);
            if (!Process32FirstW(processSnapshot, &processEntry))
            {
                systemError = GetLastError();
                CloseHandle(processSnapshot);
                errorKey = "ui.process_detection_failed";
                return false;
            }

            do
            {
                if (_wcsicmp(processEntry.szExeFile, expectedExecutable.c_str()) != 0)
                {
                    continue;
                }

                HANDLE processHandle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processEntry.th32ProcessID);
                if (processHandle == nullptr)
                {
                    continue;
                }

                std::vector<wchar_t> imagePathBuffer(32768);
                DWORD imagePathLength = static_cast<DWORD>(imagePathBuffer.size());
                const BOOL pathRead = QueryFullProcessImageNameW(
                    processHandle,
                    0,
                    imagePathBuffer.data(),
                    &imagePathLength);
                if (!pathRead)
                {
                    CloseHandle(processHandle);
                    continue;
                }

                const std::wstring imagePath(imagePathBuffer.data(), imagePathLength);
                const std::wstring lowercasePath = lowercase(imagePath);
                if (lowercasePath.find(L"\\windowsapps\\") == std::wstring::npos)
                {
                    CloseHandle(processHandle);
                    continue;
                }

                std::wstring actualFamily;
                DWORD familyError = ERROR_SUCCESS;
                if (!readPackageFamilyName(processHandle, actualFamily, familyError))
                {
                    CloseHandle(processHandle);
                    errorKey = "ui.process_detection_failed";
                    systemError = familyError;
                    return false;
                }
                CloseHandle(processHandle);
                if (_wcsicmp(actualFamily.c_str(), expectedFamily.c_str()) != 0)
                {
                    continue;
                }

                HANDLE moduleSnapshot = CreateToolhelp32Snapshot(
                    TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
                    processEntry.th32ProcessID);
                if (moduleSnapshot == INVALID_HANDLE_VALUE)
                {
                    continue;
                }

                MODULEENTRY32W moduleEntry{};
                moduleEntry.dwSize = sizeof(moduleEntry);
                const BOOL moduleFound = Module32FirstW(moduleSnapshot, &moduleEntry);
                CloseHandle(moduleSnapshot);
                if (!moduleFound || _wcsicmp(moduleEntry.szModule, expectedExecutable.c_str()) != 0)
                {
                    continue;
                }

                if (moduleEntry.modBaseSize != config.gameModuleSize())
                {
                    errorKey = "ui.unsupported_game";
                    systemError = ERROR_BAD_EXE_FORMAT;
                    return false;
                }

                std::string actualHash;
                if (!calculateSha256(imagePath, actualHash) || actualHash != expectedHash)
                {
                    errorKey = "ui.unsupported_game";
                    systemError = ERROR_BAD_EXE_FORMAT;
                    return false;
                }

                matches.push_back(ReaperProcessInfo{
                    processEntry.th32ProcessID,
                    reinterpret_cast<std::uintptr_t>(moduleEntry.modBaseAddr),
                    moduleEntry.modBaseSize,
                    imagePath,
                    false
                });
            } while (Process32NextW(processSnapshot, &processEntry));

            CloseHandle(processSnapshot);

            if (matches.empty())
            {
                errorKey = "ui.no_game";
                systemError = ERROR_NOT_FOUND;
                return false;
            }
            if (matches.size() > 1)
            {
                errorKey = "ui.multiple_games";
                systemError = ERROR_MORE_DATA;
                return false;
            }

            processInfo = std::move(matches.front());
            return true;
        }
    }

    bool ReaperGameSession::attach(
        const TrainerConfig& config,
        std::string& errorKey,
        DWORD& systemError)
    {
        if (!trackedCoinDrops_.empty())
        {
            std::int32_t restoredCoinCount{};
            if (!restoreTrackedCoinDrops(restoredCoinCount, errorKey, systemError))
            {
                return false;
            }
        }

        detach();
        systemError = ERROR_SUCCESS;

        ReaperProcessInfo processInfo;
        if (!findInstalledReaper(config, processInfo, errorKey, systemError))
        {
            return false;
        }

        DWORD openError = ERROR_SUCCESS;
        std::optional<ProcessMemory> memory = ProcessMemory::open(
            processInfo.processId,
            true,
            openError);
        if (!memory)
        {
            memory = ProcessMemory::open(processInfo.processId, false, openError);
        }
        if (!memory)
        {
            errorKey = openError == ERROR_ACCESS_DENIED
                ? "ui.access_denied"
                : "ui.memory_unavailable";
            systemError = openError;
            return false;
        }

        processInfo.writable = memory->canWrite();
        processInfo_ = std::move(processInfo);
        memory_ = std::move(memory);
        config_ = config;
        return true;
    }

    void ReaperGameSession::detach() noexcept
    {
        memory_.reset();
        processInfo_ = {};
        config_ = {};
        trackedCoinDrops_.clear();
    }

    bool ReaperGameSession::isAttached() const noexcept
    {
        return memory_.has_value() && memory_->isRunning();
    }

    const ReaperProcessInfo& ReaperGameSession::processInfo() const noexcept
    {
        return processInfo_;
    }

    bool ReaperGameSession::readActiveEntities(
        std::vector<std::uintptr_t>& entities,
        const std::string& unavailableErrorKey,
        std::string& errorKey,
        DWORD& systemError) const
    {
        entities.clear();
        systemError = ERROR_SUCCESS;
        errorKey.clear();
        if (!isAttached())
        {
            errorKey = "ui.no_game";
            systemError = ERROR_INVALID_HANDLE;
            return false;
        }

        constexpr std::int32_t maximumAttempts = 4;
        for (std::int32_t attempt = 0; attempt < maximumAttempts; ++attempt)
        {
            // The Game entity manager stores active pointers in typed buckets with separate counts.
            std::uintptr_t gameAddress{};
            std::uintptr_t gameVtable{};
            std::uintptr_t managerAddress{};
            std::uintptr_t managerVtable{};
            std::uintptr_t countsAddress{};
            std::uintptr_t countsVtable{};
            std::uintptr_t bucketsAddress{};
            std::uintptr_t bucketsVtable{};
            std::uintptr_t countsData{};
            std::uintptr_t bucketsData{};
            std::int32_t countsLength{};
            std::int32_t bucketsLength{};
            if (!readRemoteValue(
                    *memory_,
                    processInfo_.moduleBase + gameGlobalRva,
                    gameAddress,
                    systemError) ||
                gameAddress == 0 ||
                !readRemoteValue(*memory_, gameAddress, gameVtable, systemError) ||
                gameVtable != processInfo_.moduleBase + gameVtableRva ||
                !readRemoteValue(
                    *memory_,
                    gameAddress + gameEntityManagerOffset,
                    managerAddress,
                    systemError) ||
                managerAddress == 0 ||
                !readRemoteValue(*memory_, managerAddress, managerVtable, systemError) ||
                managerVtable != processInfo_.moduleBase + entityManagerVtableRva ||
                !readRemoteValue(
                    *memory_,
                    managerAddress + entityCountsOffset,
                    countsAddress,
                    systemError) ||
                countsAddress == 0 ||
                !readRemoteValue(*memory_, countsAddress, countsVtable, systemError) ||
                countsVtable != processInfo_.moduleBase + entityCountsVtableRva ||
                !readRemoteValue(
                    *memory_,
                    managerAddress + entityBucketsOffset,
                    bucketsAddress,
                    systemError) ||
                bucketsAddress == 0 ||
                !readRemoteValue(*memory_, bucketsAddress, bucketsVtable, systemError) ||
                bucketsVtable != processInfo_.moduleBase + entityBucketsVtableRva ||
                !readRemoteValue(
                    *memory_,
                    countsAddress + vectorDataOffset,
                    countsData,
                    systemError) ||
                !readRemoteValue(
                    *memory_,
                    bucketsAddress + vectorDataOffset,
                    bucketsData,
                    systemError) ||
                !memory_->readValue(
                    countsAddress + vectorLengthOffset,
                    countsLength,
                    systemError) ||
                !memory_->readValue(
                    bucketsAddress + vectorLengthOffset,
                    bucketsLength,
                    systemError))
            {
                errorKey = unavailableErrorKey;
                return false;
            }

            if (countsData == 0 ||
                bucketsData == 0 ||
                countsLength <= 0 ||
                countsLength != bucketsLength ||
                countsLength > static_cast<std::int32_t>(maximumEntityBucketCount))
            {
                errorKey = unavailableErrorKey;
                systemError = ERROR_INVALID_DATA;
                return false;
            }

            std::vector<std::uintptr_t> snapshot;
            snapshot.reserve(maximumActiveEntityCount);
            bool retrySnapshot = false;
            for (std::int32_t bucketIndex = 0; bucketIndex < countsLength; ++bucketIndex)
            {
                const std::uintptr_t countAddress =
                    countsData + static_cast<std::uintptr_t>(bucketIndex) * sizeof(std::int32_t);
                const std::uintptr_t bucketPointerAddress =
                    bucketsData + static_cast<std::uintptr_t>(bucketIndex) * sizeof(std::uintptr_t);
                std::int32_t entityCount{};
                std::uintptr_t bucketAddress{};
                std::uintptr_t bucketVtable{};
                std::uintptr_t entityData{};
                std::int32_t bucketCapacity{};
                if (!memory_->readValue(countAddress, entityCount, systemError) ||
                    !readRemoteValue(*memory_, bucketPointerAddress, bucketAddress, systemError) ||
                    bucketAddress == 0 ||
                    !readRemoteValue(*memory_, bucketAddress, bucketVtable, systemError) ||
                    bucketVtable != processInfo_.moduleBase + entityBucketVtableRva ||
                    !readRemoteValue(
                        *memory_,
                        bucketAddress + entityBucketDataOffset,
                        entityData,
                        systemError) ||
                    !memory_->readValue(
                        bucketAddress + entityBucketCapacityOffset,
                        bucketCapacity,
                        systemError))
                {
                    retrySnapshot = true;
                    break;
                }

                if (entityCount < 0 ||
                    entityCount > bucketCapacity ||
                    bucketCapacity < 0 ||
                    bucketCapacity > static_cast<std::int32_t>(maximumEntityBucketCapacity) ||
                    (entityCount > 0 && entityData == 0) ||
                    snapshot.size() + static_cast<std::size_t>(entityCount) > maximumActiveEntityCount)
                {
                    retrySnapshot = true;
                    break;
                }

                for (std::int32_t entityIndex = 0; entityIndex < entityCount; ++entityIndex)
                {
                    std::uintptr_t entityAddress{};
                    const std::uintptr_t address =
                        entityData + static_cast<std::uintptr_t>(entityIndex) * sizeof(entityAddress);
                    if (!readRemoteValue(*memory_, address, entityAddress, systemError) ||
                        entityAddress == 0)
                    {
                        retrySnapshot = true;
                        break;
                    }
                    snapshot.push_back(entityAddress);
                }
                if (retrySnapshot)
                {
                    break;
                }

                std::int32_t verifiedCount{};
                std::uintptr_t verifiedBucketAddress{};
                std::uintptr_t verifiedEntityData{};
                std::int32_t verifiedCapacity{};
                if (!memory_->readValue(countAddress, verifiedCount, systemError) ||
                    !readRemoteValue(*memory_, bucketPointerAddress, verifiedBucketAddress, systemError) ||
                    !readRemoteValue(
                        *memory_,
                        bucketAddress + entityBucketDataOffset,
                        verifiedEntityData,
                        systemError) ||
                    !memory_->readValue(
                        bucketAddress + entityBucketCapacityOffset,
                        verifiedCapacity,
                        systemError) ||
                    verifiedCount != entityCount ||
                    verifiedBucketAddress != bucketAddress ||
                    verifiedEntityData != entityData ||
                    verifiedCapacity != bucketCapacity)
                {
                    retrySnapshot = true;
                    break;
                }
            }

            std::uintptr_t verifiedGameAddress{};
            std::uintptr_t verifiedManagerAddress{};
            if (!retrySnapshot &&
                (!readRemoteValue(
                     *memory_,
                     processInfo_.moduleBase + gameGlobalRva,
                     verifiedGameAddress,
                     systemError) ||
                 !readRemoteValue(
                     *memory_,
                     verifiedGameAddress + gameEntityManagerOffset,
                     verifiedManagerAddress,
                     systemError) ||
                 verifiedGameAddress != gameAddress ||
                 verifiedManagerAddress != managerAddress))
            {
                retrySnapshot = true;
            }

            if (retrySnapshot)
            {
                continue;
            }

            entities = std::move(snapshot);
            systemError = ERROR_SUCCESS;
            errorKey.clear();
            return true;
        }

        errorKey = unavailableErrorKey;
        systemError = ERROR_RETRY;
        return false;
    }

    bool ReaperGameSession::findCurrentPlayer(
        std::uintptr_t& playerAddress,
        std::uintptr_t& transformAddress,
        std::string& errorKey,
        DWORD& systemError) const
    {
        playerAddress = 0;
        transformAddress = 0;
        systemError = ERROR_SUCCESS;
        errorKey.clear();
        if (!isAttached())
        {
            errorKey = "ui.no_game";
            systemError = ERROR_INVALID_HANDLE;
            return false;
        }

        const auto findValidatedPlayerTransform =
            [this, &systemError](std::uintptr_t candidate, std::uintptr_t& candidateTransformAddress)
        {
            std::uintptr_t vtable{};
            if (candidate == 0 ||
                !readRemoteValue(*memory_, candidate, vtable, systemError) ||
                vtable != processInfo_.moduleBase + playerVtableRva ||
                !readRemoteValue(
                    *memory_,
                    candidate + playerTransformOffset,
                    candidateTransformAddress,
                    systemError) ||
                candidateTransformAddress == 0 ||
                !readRemoteValue(*memory_, candidateTransformAddress, vtable, systemError) ||
                vtable != processInfo_.moduleBase + transformVtableRva)
            {
                return false;
            }
            return true;
        };

        for (const std::uintptr_t playerGlobalRva : {
                 currentPlayerGlobalRva,
                 worldPlayerGlobalRva
             })
        {
            std::uintptr_t globalPlayerAddress{};
            std::uintptr_t globalPlayerTransform{};
            if (readRemoteValue(
                    *memory_,
                    processInfo_.moduleBase + playerGlobalRva,
                    globalPlayerAddress,
                    systemError) &&
                findValidatedPlayerTransform(globalPlayerAddress, globalPlayerTransform))
            {
                playerAddress = globalPlayerAddress;
                transformAddress = globalPlayerTransform;
                systemError = ERROR_SUCCESS;
                return true;
            }
        }

        std::vector<std::uintptr_t> instances;
        if (!readActiveEntities(instances, "ui.player_scan_failed", errorKey, systemError))
        {
            return false;
        }

        std::vector<std::array<std::uintptr_t, 2>> validPlayers;
        for (const std::uintptr_t candidate : instances)
        {
            std::uintptr_t candidateTransformAddress{};
            if (findValidatedPlayerTransform(candidate, candidateTransformAddress))
            {
                validPlayers.push_back({ candidate, candidateTransformAddress });
            }
        }

        if (validPlayers.empty())
        {
            errorKey = "ui.player_data_not_ready";
            systemError = ERROR_NOT_FOUND;
            return false;
        }
        if (validPlayers.size() != 1)
        {
            errorKey = "ui.player_data_ambiguous";
            systemError = ERROR_MORE_DATA;
            return false;
        }

        playerAddress = validPlayers.front()[0];
        transformAddress = validPlayers.front()[1];
        systemError = ERROR_SUCCESS;
        return true;
    }

    bool ReaperGameSession::readPlayerPosition(
        PlayerPosition& position,
        std::string& errorKey,
        DWORD& systemError) const
    {
        position = {};
        std::uintptr_t playerAddress{};
        std::uintptr_t transformAddress{};
        if (!findCurrentPlayer(playerAddress, transformAddress, errorKey, systemError))
        {
            return false;
        }

        constexpr std::size_t positionReadAttempts = 4;
        for (std::size_t attempt = 0; attempt < positionReadAttempts; ++attempt)
        {
            std::int32_t xBefore{};
            std::int32_t yBefore{};
            std::int32_t transformX{};
            std::int32_t transformY{};
            std::int32_t xAfter{};
            std::int32_t yAfter{};
            if (!memory_->readValue(transformAddress + transformPositionXOffset, xBefore, systemError) ||
                !memory_->readValue(transformAddress + transformPositionYOffset, yBefore, systemError) ||
                !memory_->readValue(transformAddress + transformPositionXOffset, transformX, systemError) ||
                !memory_->readValue(transformAddress + transformPositionYOffset, transformY, systemError) ||
                !memory_->readValue(transformAddress + transformPositionXOffset, xAfter, systemError) ||
                !memory_->readValue(transformAddress + transformPositionYOffset, yAfter, systemError))
            {
                errorKey = "ui.player_data_not_ready";
                return false;
            }

            if (xBefore == xAfter && yBefore == yAfter &&
                transformX == xAfter && transformY == yAfter)
            {
                position.playerAddress = playerAddress;
                position.transformAddress = transformAddress;
                position.xQ16 = xAfter;
                position.yQ16 = yAfter;
                errorKey.clear();
                systemError = ERROR_SUCCESS;
                return true;
            }
        }

        errorKey = "ui.player_position_unstable";
        systemError = ERROR_INVALID_DATA;
        return false;
    }

    bool ReaperGameSession::readCurrentPlayer(
        PlayerEntityInfo& player,
        std::string& errorKey,
        DWORD& systemError) const
    {
        player = {};
        std::uintptr_t playerAddress{};
        std::uintptr_t transformAddress{};
        if (!findCurrentPlayer(playerAddress, transformAddress, errorKey, systemError))
        {
            return false;
        }

        std::uintptr_t progressAddress{};
        if (!findCurrentProgress(progressAddress, errorKey, systemError))
        {
            return false;
        }

        player.playerAddress = playerAddress;
        player.progressAddress = progressAddress;
        player.transformAddress = transformAddress;
        errorKey.clear();
        systemError = ERROR_SUCCESS;
        return true;
    }

    bool ReaperGameSession::readCoinDrops(
        std::vector<CoinDropInfo>& drops,
        std::int32_t& sparkEntityCount,
        std::string& errorKey,
        DWORD& systemError) const
    {
        drops.clear();
        sparkEntityCount = 0;
        systemError = ERROR_SUCCESS;
        errorKey.clear();
        std::vector<std::uintptr_t> entities;
        if (!readActiveEntities(entities, "ui.coin_scan_failed", errorKey, systemError))
        {
            return false;
        }

        std::int32_t gameClock{};
        if (!memory_->readValue(
                processInfo_.moduleBase + gameClockRva,
                gameClock,
                systemError))
        {
            errorKey = "ui.coin_scan_failed";
            return false;
        }

        for (const std::uintptr_t address : entities)
        {
            std::uintptr_t vtable{};
            if (!readRemoteValue(*memory_, address, vtable, systemError))
            {
                continue;
            }
            if (vtable == processInfo_.moduleBase + sparkEntityVtableRva)
            {
                ++sparkEntityCount;
                continue;
            }
            if (vtable != processInfo_.moduleBase + lootItemVtableRva)
            {
                continue;
            }

            std::int32_t value{};
            std::int32_t spawnTick{};
            std::int32_t remainingBounces{};
            std::int32_t xQ16{};
            std::int32_t yQ16{};
            std::int32_t velocityXQ16{};
            std::int32_t velocityYQ16{};
            std::uintptr_t visualAddress{};
            std::uintptr_t visualVtable{};
            if (!memory_->readValue(address + lootValueOffset, value, systemError) ||
                (value != 5 && value != 25) ||
                !memory_->readValue(address + lootSpawnTickOffset, spawnTick, systemError) ||
                spawnTick == std::numeric_limits<std::int32_t>::max() ||
                !memory_->readValue(address + lootBounceCountOffset, remainingBounces, systemError) ||
                !memory_->readValue(address + lootPositionXOffset, xQ16, systemError) ||
                !memory_->readValue(address + lootPositionYOffset, yQ16, systemError) ||
                !memory_->readValue(address + lootVelocityXOffset, velocityXQ16, systemError) ||
                !memory_->readValue(address + lootVelocityYOffset, velocityYQ16, systemError) ||
                !memory_->readValue(address + lootVisualOffset, visualAddress, systemError) ||
                visualAddress == 0 ||
                !readRemoteValue(*memory_, visualAddress, visualVtable, systemError) ||
                visualVtable == 0)
            {
                continue;
            }

            CoinDropInfo drop;
            drop.address = address;
            drop.value = value;
            drop.spawnTick = spawnTick;
            drop.gameClock = gameClock;
            const std::uint32_t pickupAgeBits =
                static_cast<std::uint32_t>(gameClock) - static_cast<std::uint32_t>(spawnTick);
            drop.pickupAgeTicks = std::bit_cast<std::int32_t>(pickupAgeBits);
            drop.pickupEligible = drop.pickupAgeTicks >= 10;
            drop.remainingBounces = remainingBounces;
            drop.xQ16 = xQ16;
            drop.yQ16 = yQ16;
            drop.velocityXQ16 = velocityXQ16;
            drop.velocityYQ16 = velocityYQ16;
            drop.visualAddress = visualAddress;
            drops.push_back(drop);
        }

        systemError = ERROR_SUCCESS;
        return true;
    }

    bool ReaperGameSession::moveEligibleCoinDropsTowardPlayer(
        std::int32_t stepPixels,
        CoinAutoCollectStatus& status,
        std::string& errorKey,
        DWORD& systemError)
    {
        status = {};
        systemError = ERROR_SUCCESS;
        errorKey.clear();
        if (!isAttached())
        {
            errorKey = "ui.no_game";
            systemError = ERROR_INVALID_HANDLE;
            return false;
        }
        if (!processInfo_.writable || !memory_->canWrite())
        {
            errorKey = "ui.read_only";
            systemError = ERROR_ACCESS_DENIED;
            return false;
        }
        if (stepPixels <= 0 || stepPixels > 128)
        {
            errorKey = "ui.invalid_value";
            systemError = ERROR_INVALID_PARAMETER;
            return false;
        }

        PlayerPosition playerPosition;
        if (!readPlayerPosition(playerPosition, errorKey, systemError))
        {
            return false;
        }

        std::vector<CoinDropInfo> drops;
        std::int32_t sparkEntityCount{};
        if (!readCoinDrops(drops, sparkEntityCount, errorKey, systemError))
        {
            return false;
        }

        status.activeCoinCount = static_cast<std::int32_t>(drops.size());
        std::unordered_set<std::uintptr_t> activeCoinAddresses;
        activeCoinAddresses.reserve(drops.size());
        for (const CoinDropInfo& drop : drops)
        {
            activeCoinAddresses.insert(drop.address);
            if (drop.pickupEligible)
            {
                ++status.eligibleCoinCount;
            }
        }

        std::int32_t gameClock{};
        if (!memory_->readValue(
                processInfo_.moduleBase + gameClockRva,
                gameClock,
                systemError))
        {
            errorKey = "ui.coin_scan_failed";
            return false;
        }

        for (auto tracked = trackedCoinDrops_.begin(); tracked != trackedCoinDrops_.end();)
        {
            if (activeCoinAddresses.find(tracked->first) == activeCoinAddresses.end())
            {
                tracked = trackedCoinDrops_.erase(tracked);
                continue;
            }

            if (tracked->second.blockedAfterTimeout)
            {
                ++tracked;
                continue;
            }

            std::uintptr_t vtable{};
            std::uintptr_t visualAddress{};
            std::int32_t value{};
            std::int32_t spawnTick{};
            if (!readRemoteValue(*memory_, tracked->first, vtable, systemError) ||
                !memory_->readValue(tracked->first + lootValueOffset, value, systemError) ||
                !memory_->readValue(tracked->first + lootSpawnTickOffset, spawnTick, systemError) ||
                !memory_->readValue(tracked->first + lootVisualOffset, visualAddress, systemError))
            {
                errorKey = "ui.coin_scan_failed";
                return false;
            }

            if (vtable != processInfo_.moduleBase + lootItemVtableRva ||
                value != tracked->second.value ||
                spawnTick != tracked->second.spawnTick ||
                visualAddress != tracked->second.visualAddress)
            {
                tracked = trackedCoinDrops_.erase(tracked);
                continue;
            }

            const auto trackedDuration =
                std::chrono::steady_clock::now() - tracked->second.firstMovedAt;
            if (trackedDuration >= std::chrono::milliseconds(config_.autoCollectRecoveryMilliseconds()))
            {
                if (!memory_->writeBytes(
                        tracked->first + lootPositionXOffset,
                        tracked->second.originalMotion.data(),
                        sizeof(tracked->second.originalMotion),
                        systemError))
                {
                    errorKey = "ui.write_restore_failed";
                    return false;
                }

                std::array<std::int32_t, 4> restoredMotion{};
                if (!memory_->readBytes(
                        tracked->first + lootPositionXOffset,
                        restoredMotion.data(),
                        sizeof(restoredMotion),
                        systemError) ||
                    restoredMotion != tracked->second.originalMotion)
                {
                    errorKey = "ui.write_restore_failed";
                    if (systemError == ERROR_SUCCESS)
                    {
                        systemError = ERROR_INVALID_DATA;
                    }
                    return false;
                }

                tracked->second.blockedAfterTimeout = true;
                ++status.restoredCoinCount;
            }

            ++tracked;
        }

        const double maximumStepQ16 = static_cast<double>(stepPixels) * 65536.0;
        for (const CoinDropInfo& drop : drops)
        {
            if (!drop.pickupEligible ||
                activeCoinAddresses.find(drop.address) == activeCoinAddresses.end())
            {
                continue;
            }
            const auto existingTrack = trackedCoinDrops_.find(drop.address);
            if (existingTrack != trackedCoinDrops_.end() &&
                existingTrack->second.blockedAfterTimeout)
            {
                continue;
            }

            std::uintptr_t vtable{};
            std::uintptr_t visualAddress{};
            std::int32_t value{};
            std::int32_t spawnTick{};
            std::int32_t xQ16{};
            std::int32_t yQ16{};
            std::int32_t velocityXQ16{};
            std::int32_t velocityYQ16{};
            if (!readRemoteValue(*memory_, drop.address, vtable, systemError) ||
                !memory_->readValue(drop.address + lootValueOffset, value, systemError) ||
                !memory_->readValue(drop.address + lootSpawnTickOffset, spawnTick, systemError) ||
                !memory_->readValue(drop.address + lootVisualOffset, visualAddress, systemError) ||
                !memory_->readValue(drop.address + lootPositionXOffset, xQ16, systemError) ||
                !memory_->readValue(drop.address + lootPositionYOffset, yQ16, systemError) ||
                !memory_->readValue(drop.address + lootVelocityXOffset, velocityXQ16, systemError) ||
                !memory_->readValue(drop.address + lootVelocityYOffset, velocityYQ16, systemError))
            {
                errorKey = "ui.coin_scan_failed";
                return false;
            }

            const std::uint32_t ageBits =
                static_cast<std::uint32_t>(gameClock) - static_cast<std::uint32_t>(spawnTick);
            const std::int32_t pickupAgeTicks = std::bit_cast<std::int32_t>(ageBits);
            if (vtable != processInfo_.moduleBase + lootItemVtableRva ||
                value != drop.value ||
                spawnTick != drop.spawnTick ||
                spawnTick == std::numeric_limits<std::int32_t>::max() ||
                visualAddress == 0 ||
                visualAddress != drop.visualAddress ||
                pickupAgeTicks < 10)
            {
                continue;
            }

            const double deltaX = static_cast<double>(playerPosition.xQ16) - xQ16;
            const double deltaY = static_cast<double>(playerPosition.yQ16) - yQ16;
            const double distanceQ16 = std::hypot(deltaX, deltaY);
            if (!std::isfinite(distanceQ16))
            {
                errorKey = "ui.coin_scan_failed";
                systemError = ERROR_INVALID_DATA;
                return false;
            }

            std::int32_t targetXQ16{};
            std::int32_t targetYQ16{};
            if (distanceQ16 <= maximumStepQ16)
            {
                targetXQ16 = playerPosition.xQ16;
                targetYQ16 = playerPosition.yQ16;
            }
            else
            {
                const double ratio = maximumStepQ16 / distanceQ16;
                const double nextX = static_cast<double>(xQ16) + deltaX * ratio;
                const double nextY = static_cast<double>(yQ16) + deltaY * ratio;
                if (!std::isfinite(nextX) ||
                    !std::isfinite(nextY) ||
                    nextX < std::numeric_limits<std::int32_t>::min() ||
                    nextX > std::numeric_limits<std::int32_t>::max() ||
                    nextY < std::numeric_limits<std::int32_t>::min() ||
                    nextY > std::numeric_limits<std::int32_t>::max())
                {
                    errorKey = "ui.coin_scan_failed";
                    systemError = ERROR_ARITHMETIC_OVERFLOW;
                    return false;
                }
                targetXQ16 = static_cast<std::int32_t>(std::round(nextX));
                targetYQ16 = static_cast<std::int32_t>(std::round(nextY));
            }

            if (targetXQ16 == xQ16 &&
                targetYQ16 == yQ16 &&
                velocityXQ16 == 0 &&
                velocityYQ16 == 0)
            {
                continue;
            }

            auto tracked = trackedCoinDrops_.find(drop.address);
            if (tracked != trackedCoinDrops_.end() &&
                (tracked->second.value != value ||
                 tracked->second.spawnTick != spawnTick ||
                 tracked->second.visualAddress != visualAddress))
            {
                trackedCoinDrops_.erase(tracked);
                tracked = trackedCoinDrops_.end();
            }
            if (tracked == trackedCoinDrops_.end())
            {
                const TrackedCoinDrop original{
                    value,
                    spawnTick,
                    visualAddress,
                    { xQ16, yQ16, velocityXQ16, velocityYQ16 },
                    std::chrono::steady_clock::now(),
                    false
                };
                tracked = trackedCoinDrops_.emplace(drop.address, original).first;
            }

            const std::array<std::int32_t, 4> nextMotion{
                targetXQ16,
                targetYQ16,
                0,
                0
            };
            if (!memory_->writeBytes(
                    drop.address + lootPositionXOffset,
                    nextMotion.data(),
                    sizeof(nextMotion),
                    systemError))
            {
                errorKey = "ui.write_failed";
                return false;
            }

            std::array<std::int32_t, 4> verifiedMotion{};
            if (!memory_->readBytes(
                    drop.address + lootPositionXOffset,
                    verifiedMotion.data(),
                    sizeof(verifiedMotion),
                    systemError))
            {
                errorKey = "ui.write_verify_failed";
                return false;
            }
            if (verifiedMotion != nextMotion)
            {
                errorKey = "ui.write_verify_failed";
                systemError = ERROR_INVALID_DATA;
                return false;
            }

            ++status.movedCoinCount;
        }

        for (const auto& [address, tracked] : trackedCoinDrops_)
        {
            static_cast<void>(address);
            if (!tracked.blockedAfterTimeout)
            {
                ++status.trackedCoinCount;
            }
        }
        errorKey.clear();
        systemError = ERROR_SUCCESS;
        return true;
    }

    bool ReaperGameSession::restoreTrackedCoinDrops(
        std::int32_t& restoredCoinCount,
        std::string& errorKey,
        DWORD& systemError)
    {
        restoredCoinCount = 0;
        systemError = ERROR_SUCCESS;
        errorKey.clear();
        if (trackedCoinDrops_.empty())
        {
            return true;
        }
        if (!isAttached())
        {
            errorKey = "ui.no_game";
            systemError = ERROR_INVALID_HANDLE;
            return false;
        }
        if (!processInfo_.writable || !memory_->canWrite())
        {
            errorKey = "ui.read_only";
            systemError = ERROR_ACCESS_DENIED;
            return false;
        }

        std::vector<std::uintptr_t> activeEntities;
        if (!readActiveEntities(activeEntities, "ui.coin_scan_failed", errorKey, systemError))
        {
            return false;
        }
        const std::unordered_set<std::uintptr_t> activeEntitySet(
            activeEntities.begin(),
            activeEntities.end());

        for (auto tracked = trackedCoinDrops_.begin(); tracked != trackedCoinDrops_.end();)
        {
            if (tracked->second.blockedAfterTimeout)
            {
                tracked = trackedCoinDrops_.erase(tracked);
                continue;
            }
            if (activeEntitySet.find(tracked->first) == activeEntitySet.end())
            {
                tracked = trackedCoinDrops_.erase(tracked);
                continue;
            }

            std::uintptr_t vtable{};
            std::uintptr_t visualAddress{};
            std::int32_t value{};
            std::int32_t spawnTick{};
            if (!readRemoteValue(*memory_, tracked->first, vtable, systemError) ||
                !memory_->readValue(tracked->first + lootValueOffset, value, systemError) ||
                !memory_->readValue(tracked->first + lootSpawnTickOffset, spawnTick, systemError) ||
                !memory_->readValue(tracked->first + lootVisualOffset, visualAddress, systemError))
            {
                errorKey = "ui.coin_scan_failed";
                return false;
            }

            if (vtable != processInfo_.moduleBase + lootItemVtableRva ||
                value != tracked->second.value ||
                spawnTick != tracked->second.spawnTick ||
                visualAddress != tracked->second.visualAddress)
            {
                tracked = trackedCoinDrops_.erase(tracked);
                continue;
            }

            if (!memory_->writeBytes(
                    tracked->first + lootPositionXOffset,
                    tracked->second.originalMotion.data(),
                    sizeof(tracked->second.originalMotion),
                    systemError))
            {
                errorKey = "ui.write_restore_failed";
                return false;
            }

            std::array<std::int32_t, 4> restoredMotion{};
            if (!memory_->readBytes(
                    tracked->first + lootPositionXOffset,
                    restoredMotion.data(),
                    sizeof(restoredMotion),
                    systemError) ||
                restoredMotion != tracked->second.originalMotion)
            {
                errorKey = "ui.write_restore_failed";
                if (systemError == ERROR_SUCCESS)
                {
                    systemError = ERROR_INVALID_DATA;
                }
                return false;
            }

            ++restoredCoinCount;
            tracked = trackedCoinDrops_.erase(tracked);
        }

        systemError = ERROR_SUCCESS;
        errorKey.clear();
        return true;
    }

    bool ReaperGameSession::readPersistentGold(
        std::int32_t& value,
        std::string& errorKey,
        DWORD& systemError) const
    {
        value = 0;
        std::uintptr_t progressAddress{};
        if (!findCurrentProgress(progressAddress, errorKey, systemError))
        {
            return false;
        }

        std::int32_t walletIndex{};
        if (!memory_->readValue(
                progressAddress + persistentGoldIndexOffset,
                walletIndex,
                systemError))
        {
            errorKey = "ui.money_data_not_ready";
            return false;
        }
        std::uintptr_t valueAddress{};
        std::int32_t xorKey{};
        std::int32_t storedValue{};
        return readMappedValue(
            walletIndex,
            valueAddress,
            xorKey,
            storedValue,
            value,
            "ui.money_data_not_ready",
            "ui.money_data_invalid",
            "ui.money_data_changed",
            errorKey,
            systemError);
    }

    bool ReaperGameSession::setPersistentGold(
        std::int32_t value,
        std::string& errorKey,
        DWORD& systemError)
    {
        if (value < 0)
        {
            errorKey = "ui.money_range";
            systemError = ERROR_INVALID_PARAMETER;
            return false;
        }
        std::int32_t currentValue{};
        if (!readPersistentGold(currentValue, errorKey, systemError))
        {
            return false;
        }
        return writePersistentGold(value, currentValue, errorKey, systemError);
    }

    bool ReaperGameSession::addPersistentGold(
        std::int32_t amount,
        std::int32_t& result,
        std::string& errorKey,
        DWORD& systemError)
    {
        result = 0;
        if (amount <= 0)
        {
            errorKey = "ui.money_range";
            systemError = ERROR_INVALID_PARAMETER;
            return false;
        }

        std::int32_t current{};
        if (!readPersistentGold(current, errorKey, systemError))
        {
            return false;
        }
        if (amount > std::numeric_limits<std::int32_t>::max() - current)
        {
            errorKey = "ui.money_range";
            systemError = ERROR_ARITHMETIC_OVERFLOW;
            return false;
        }

        result = current + amount;
        return writePersistentGold(result, current, errorKey, systemError);
    }

    bool ReaperGameSession::findCurrentProgress(
        std::uintptr_t& progressAddress,
        std::string& errorKey,
        DWORD& systemError) const
    {
        progressAddress = 0;
        systemError = ERROR_SUCCESS;
        if (!isAttached())
        {
            errorKey = "ui.no_game";
            systemError = ERROR_INVALID_HANDLE;
            return false;
        }

        std::string playerErrorKey;
        DWORD playerSystemError = ERROR_SUCCESS;
        std::uintptr_t playerAddress{};
        std::uintptr_t transformAddress{};
        if (findCurrentPlayer(playerAddress, transformAddress, playerErrorKey, playerSystemError))
        {
            std::uintptr_t combatProgressAddress{};
            std::uintptr_t combatProgressVtable{};
            if (readRemoteValue(
                    *memory_,
                    playerAddress + 0x230,
                    combatProgressAddress,
                    playerSystemError) &&
                combatProgressAddress != 0 &&
                readRemoteValue(
                    *memory_,
                    combatProgressAddress,
                    combatProgressVtable,
                    playerSystemError) &&
                combatProgressVtable == processInfo_.moduleBase + playerProgressVtableRva)
            {
                progressAddress = combatProgressAddress;
                errorKey.clear();
                systemError = ERROR_SUCCESS;
                return true;
            }
            playerErrorKey = "ui.progress_data_not_ready";
        }

        DWORD mainSystemError = ERROR_SUCCESS;
        std::uintptr_t mainAddress{};
        std::uintptr_t mainVtable{};
        std::uintptr_t mainCampaignAddress{};
        std::uintptr_t mainCampaignVtable{};
        std::uintptr_t mainProgressAddress{};
        std::uintptr_t mainProgressVtable{};
        if (readRemoteValue(
                *memory_,
                processInfo_.moduleBase + mainGlobalRva,
                mainAddress,
                mainSystemError) &&
            mainAddress != 0 &&
            readRemoteValue(*memory_, mainAddress, mainVtable, mainSystemError) &&
            mainVtable == processInfo_.moduleBase + mainVtableRva &&
            readRemoteValue(
                *memory_,
                mainAddress + mainCampaignOffset,
                mainCampaignAddress,
                mainSystemError) &&
            mainCampaignAddress != 0 &&
            readRemoteValue(
                *memory_,
                mainCampaignAddress,
                mainCampaignVtable,
                mainSystemError) &&
            mainCampaignVtable == processInfo_.moduleBase + campaignVtableRva &&
            readRemoteValue(
                *memory_,
                mainCampaignAddress + campaignProgressOffset,
                mainProgressAddress,
                mainSystemError) &&
            mainProgressAddress != 0 &&
            readRemoteValue(
                *memory_,
                mainProgressAddress,
                mainProgressVtable,
                mainSystemError) &&
            mainProgressVtable == processInfo_.moduleBase + playerProgressVtableRva)
        {
            progressAddress = mainProgressAddress;
            errorKey.clear();
            systemError = ERROR_SUCCESS;
            return true;
        }
        std::string selectorErrorKey;
        DWORD selectorSystemError = ERROR_SUCCESS;
        const auto findProgressFromSelector = [&]()
        {
            std::uintptr_t selectorAddress{};
            if (!readRemoteValue(
                    *memory_,
                    processInfo_.moduleBase + levelupSelectorGlobalRva,
                    selectorAddress,
                    selectorSystemError) ||
                selectorAddress == 0)
            {
                selectorErrorKey = "ui.progress_data_not_ready";
                return false;
            }

            std::uintptr_t vtable{};
            if (!readRemoteValue(*memory_, selectorAddress, vtable, selectorSystemError) ||
                vtable != processInfo_.moduleBase + levelupSelectorVtableRva)
            {
                selectorErrorKey = "ui.progress_data_not_ready";
                selectorSystemError = ERROR_INVALID_DATA;
                return false;
            }

            std::uintptr_t campaignAddress{};
            if (!readRemoteValue(
                    *memory_,
                    selectorAddress + 0x28,
                    campaignAddress,
                    selectorSystemError) ||
                campaignAddress == 0)
            {
                selectorErrorKey = "ui.progress_data_not_ready";
                return false;
            }

            if (!readRemoteValue(*memory_, campaignAddress, vtable, selectorSystemError) ||
                vtable != processInfo_.moduleBase + campaignVtableRva)
            {
                selectorErrorKey = "ui.progress_data_not_ready";
                selectorSystemError = ERROR_INVALID_DATA;
                return false;
            }

            std::uintptr_t candidateProgressAddress{};
            if (!readRemoteValue(
                    *memory_,
                    campaignAddress + campaignProgressOffset,
                    candidateProgressAddress,
                    selectorSystemError) ||
                candidateProgressAddress == 0)
            {
                selectorErrorKey = "ui.progress_data_not_ready";
                return false;
            }

            if (!readRemoteValue(*memory_, candidateProgressAddress, vtable, selectorSystemError) ||
                vtable != processInfo_.moduleBase + playerProgressVtableRva)
            {
                selectorErrorKey = "ui.progress_data_not_ready";
                selectorSystemError = ERROR_INVALID_DATA;
                return false;
            }

            progressAddress = candidateProgressAddress;
            return true;
        };

        if (findProgressFromSelector())
        {
            errorKey.clear();
            systemError = ERROR_SUCCESS;
            return true;
        }

        if (playerErrorKey == "ui.player_data_ambiguous" ||
            playerErrorKey == "ui.player_scan_failed")
        {
            errorKey = playerErrorKey;
            systemError = playerSystemError;
            return false;
        }

        errorKey = selectorErrorKey.empty() ? "ui.progress_data_not_ready" : selectorErrorKey;
        systemError = playerSystemError != ERROR_SUCCESS
            ? playerSystemError
            : (mainSystemError != ERROR_SUCCESS ? mainSystemError : selectorSystemError);
        return false;
    }

    bool ReaperGameSession::locateMappedValueAddress(
        std::int32_t index,
        std::uintptr_t& valueAddress,
        std::int32_t& xorKey,
        const std::string& unavailableErrorKey,
        std::string& errorKey,
        DWORD& systemError) const
    {
        valueAddress = 0;
        xorKey = 0;
        if (!isAttached())
        {
            errorKey = "ui.no_game";
            systemError = ERROR_INVALID_HANDLE;
            return false;
        }
        if (index < 0 || index > 64)
        {
            errorKey = unavailableErrorKey;
            systemError = ERROR_INVALID_DATA;
            return false;
        }

        if (!memory_->readValue(processInfo_.moduleBase + pointXorKeyRva, xorKey, systemError))
        {
            errorKey = unavailableErrorKey;
            return false;
        }

        std::uintptr_t mappingObject{};
        std::uintptr_t mappingData{};
        std::uintptr_t pointsObject{};
        std::uintptr_t pointsData{};
        std::int32_t mappingLength{};
        std::int32_t pointsLength{};
        if (!memory_->readValue(processInfo_.moduleBase + pointIndexMapGlobalRva, mappingObject, systemError) ||
            mappingObject == 0 ||
            !memory_->readValue(mappingObject + vectorDataOffset, mappingData, systemError) ||
            mappingData == 0 ||
            !memory_->readValue(mappingObject + vectorLengthOffset, mappingLength, systemError) ||
            index >= mappingLength ||
            !memory_->readValue(processInfo_.moduleBase + pointValuesGlobalRva, pointsObject, systemError) ||
            pointsObject == 0 ||
            !memory_->readValue(pointsObject + vectorDataOffset, pointsData, systemError) ||
            pointsData == 0 ||
            !memory_->readValue(pointsObject + vectorLengthOffset, pointsLength, systemError))
        {
            errorKey = unavailableErrorKey;
            systemError = ERROR_INVALID_DATA;
            return false;
        }

        std::int16_t mappedIndex{};
        if (!memory_->readValue(
                mappingData + static_cast<std::uintptr_t>(index) * sizeof(mappedIndex),
                mappedIndex,
                systemError) ||
            mappedIndex < 0 || mappedIndex >= pointsLength)
        {
            errorKey = unavailableErrorKey;
            systemError = ERROR_INVALID_DATA;
            return false;
        }

        valueAddress = pointsData + static_cast<std::uintptr_t>(mappedIndex) * sizeof(std::int32_t);
        errorKey.clear();
        systemError = ERROR_SUCCESS;
        return true;
    }

    bool ReaperGameSession::readMappedValue(
        std::int32_t index,
        std::uintptr_t& valueAddress,
        std::int32_t& xorKey,
        std::int32_t& storedValue,
        std::int32_t& value,
        const std::string& unavailableErrorKey,
        const std::string& invalidErrorKey,
        const std::string& changedErrorKey,
        std::string& errorKey,
        DWORD& systemError) const
    {
        valueAddress = 0;
        xorKey = 0;
        storedValue = 0;
        value = 0;

        constexpr std::int32_t maximumAttempts = 6;
        // Reaper rotates this XOR key while updating mapped values, so accept only a stable pair.
        for (std::int32_t attempt = 0; attempt < maximumAttempts; ++attempt)
        {
            if (!locateMappedValueAddress(
                    index,
                    valueAddress,
                    xorKey,
                    unavailableErrorKey,
                    errorKey,
                    systemError))
            {
                return false;
            }

            std::int32_t confirmedKey{};
            if (!memory_->readValue(valueAddress, storedValue, systemError) ||
                !memory_->readValue(
                    processInfo_.moduleBase + pointXorKeyRva,
                    confirmedKey,
                    systemError))
            {
                errorKey = unavailableErrorKey;
                return false;
            }
            if (confirmedKey != xorKey)
            {
                continue;
            }

            value = storedValue ^ xorKey;
            if (value < 0)
            {
                errorKey = invalidErrorKey;
                systemError = ERROR_INVALID_DATA;
                return false;
            }

            errorKey.clear();
            systemError = ERROR_SUCCESS;
            return true;
        }

        errorKey = changedErrorKey;
        systemError = ERROR_RETRY;
        return false;
    }

    bool ReaperGameSession::locateSkillPointAddress(
        std::uintptr_t progressAddress,
        std::uintptr_t& valueAddress,
        std::int32_t& xorKey,
        std::string& errorKey,
        DWORD& systemError) const
    {
        std::int32_t progressIndex{};
        if (!memory_->readValue(progressAddress + 0x20, progressIndex, systemError))
        {
            errorKey = "ui.skill_data_not_ready";
            return false;
        }
        return locateMappedValueAddress(
            progressIndex,
            valueAddress,
            xorKey,
            "ui.skill_data_not_ready",
            errorKey,
            systemError);
    }

    bool ReaperGameSession::writePersistentGold(
        std::int32_t value,
        std::int32_t expectedValue,
        std::string& errorKey,
        DWORD& systemError)
    {
        if (!isAttached())
        {
            errorKey = "ui.no_game";
            systemError = ERROR_INVALID_HANDLE;
            return false;
        }
        if (!memory_->canWrite())
        {
            errorKey = "ui.read_only";
            systemError = ERROR_ACCESS_DENIED;
            return false;
        }

        std::uintptr_t progressAddress{};
        if (!findCurrentProgress(progressAddress, errorKey, systemError))
        {
            return false;
        }

        std::int32_t walletIndex{};
        if (!memory_->readValue(
                progressAddress + persistentGoldIndexOffset,
                walletIndex,
                systemError))
        {
            errorKey = "ui.money_data_not_ready";
            return false;
        }

        constexpr std::int32_t maximumAttempts = 4;
        for (std::int32_t attempt = 0; attempt < maximumAttempts; ++attempt)
        {
            std::uintptr_t valueAddress{};
            std::int32_t xorKey{};
            std::int32_t previousStored{};
            std::int32_t currentValue{};
            if (!readMappedValue(
                    walletIndex,
                    valueAddress,
                    xorKey,
                    previousStored,
                    currentValue,
                    "ui.money_data_not_ready",
                    "ui.money_data_invalid",
                    "ui.money_data_changed",
                    errorKey,
                    systemError))
            {
                if (errorKey == "ui.money_data_changed")
                {
                    continue;
                }
                return false;
            }

            if (currentValue == value)
            {
                errorKey.clear();
                systemError = ERROR_SUCCESS;
                return true;
            }
            if (currentValue != expectedValue)
            {
                errorKey = "ui.money_data_changed";
                systemError = ERROR_RETRY;
                return false;
            }

            std::int32_t keyBeforeWrite{};
            std::int32_t storedBeforeWrite{};
            std::int32_t keyAfterWrite{};
            if (!memory_->readValue(
                    processInfo_.moduleBase + pointXorKeyRva,
                    keyBeforeWrite,
                    systemError) ||
                !memory_->readValue(valueAddress, storedBeforeWrite, systemError) ||
                !memory_->readValue(
                    processInfo_.moduleBase + pointXorKeyRva,
                    keyAfterWrite,
                    systemError))
            {
                errorKey = "ui.money_data_not_ready";
                return false;
            }
            if (keyBeforeWrite != xorKey ||
                keyAfterWrite != xorKey ||
                storedBeforeWrite != previousStored)
            {
                continue;
            }

            const std::int32_t encodedValue = value ^ xorKey;
            if (!memory_->writeValue(valueAddress, encodedValue, systemError))
            {
                errorKey = "ui.write_failed";
                return false;
            }

            std::int32_t actualStored{};
            std::int32_t actualKey{};
            if (memory_->readValue(valueAddress, actualStored, systemError) &&
                memory_->readValue(
                    processInfo_.moduleBase + pointXorKeyRva,
                    actualKey,
                    systemError) &&
                actualKey == xorKey &&
                actualStored == encodedValue &&
                (actualStored ^ actualKey) == value)
            {
                errorKey.clear();
                systemError = ERROR_SUCCESS;
                return true;
            }

            std::uintptr_t verifiedAddress{};
            std::int32_t verifiedKey{};
            std::int32_t verifiedStored{};
            std::int32_t verifiedValue{};
            if (!readMappedValue(
                    walletIndex,
                    verifiedAddress,
                    verifiedKey,
                    verifiedStored,
                    verifiedValue,
                    "ui.money_data_not_ready",
                    "ui.money_data_invalid",
                    "ui.money_data_changed",
                    errorKey,
                    systemError))
            {
                if (errorKey == "ui.money_data_changed")
                {
                    continue;
                }
                return false;
            }
            if (verifiedValue == value)
            {
                errorKey.clear();
                systemError = ERROR_SUCCESS;
                return true;
            }
            if (verifiedValue != expectedValue)
            {
                errorKey = "ui.money_data_changed";
                systemError = ERROR_RETRY;
                return false;
            }
        }

        errorKey = "ui.money_data_changed";
        systemError = ERROR_RETRY;
        return false;
    }

    bool ReaperGameSession::readSkillChoices(
        std::int32_t& value,
        std::string& errorKey,
        DWORD& systemError) const
    {
        value = 0;
        std::uintptr_t progressAddress{};
        if (!findCurrentProgress(progressAddress, errorKey, systemError))
        {
            return false;
        }

        std::uintptr_t valueAddress{};
        std::int32_t xorKey{};
        std::int32_t storedValue{};
        if (!locateSkillPointAddress(progressAddress, valueAddress, xorKey, errorKey, systemError) ||
            !memory_->readValue(valueAddress, storedValue, systemError))
        {
            errorKey = "ui.skill_data_not_ready";
            return false;
        }

        value = storedValue ^ xorKey;
        if (value < 0)
        {
            errorKey = "ui.skill_data_invalid";
            systemError = ERROR_INVALID_DATA;
            return false;
        }
        return true;
    }

    bool ReaperGameSession::setSkillChoices(
        std::int32_t value,
        std::string& errorKey,
        DWORD& systemError)
    {
        if (value < 0 || value > config_.maximumSkillChoices())
        {
            errorKey = "ui.skill_choices_range";
            systemError = ERROR_INVALID_PARAMETER;
            return false;
        }
        if (!isAttached() || !memory_->canWrite())
        {
            errorKey = "ui.read_only";
            systemError = ERROR_ACCESS_DENIED;
            return false;
        }

        std::uintptr_t progressAddress{};
        if (!findCurrentProgress(progressAddress, errorKey, systemError))
        {
            return false;
        }

        std::uintptr_t valueAddress{};
        std::int32_t xorKey{};
        if (!locateSkillPointAddress(progressAddress, valueAddress, xorKey, errorKey, systemError))
        {
            return false;
        }

        std::int32_t previousStored{};
        if (!memory_->readValue(valueAddress, previousStored, systemError))
        {
            errorKey = "ui.skill_data_not_ready";
            return false;
        }

        const std::int32_t encodedValue = value ^ xorKey;
        if (!memory_->writeValue(valueAddress, encodedValue, systemError))
        {
            errorKey = "ui.write_failed";
            return false;
        }

        std::int32_t actualStored{};
        if (!memory_->readValue(valueAddress, actualStored, systemError) ||
            actualStored != encodedValue)
        {
            DWORD restoreError = ERROR_SUCCESS;
            const bool restored = memory_->writeValue(valueAddress, previousStored, restoreError);
            errorKey = restored ? "ui.write_verify_failed" : "ui.write_restore_failed";
            systemError = restored ? ERROR_INVALID_DATA : restoreError;
            return false;
        }

        return true;
    }

    bool ReaperGameSession::addSkillChoices(
        std::int32_t amount,
        std::int32_t& result,
        std::string& errorKey,
        DWORD& systemError)
    {
        result = 0;
        if (amount <= 0)
        {
            errorKey = "ui.invalid_value";
            systemError = ERROR_INVALID_PARAMETER;
            return false;
        }

        std::int32_t current{};
        if (!readSkillChoices(current, errorKey, systemError))
        {
            return false;
        }

        if (amount > config_.maximumSkillChoices() - current)
        {
            errorKey = "ui.skill_choices_range";
            systemError = ERROR_ARITHMETIC_OVERFLOW;
            return false;
        }

        result = current + amount;
        return setSkillChoices(result, errorKey, systemError);
    }

    bool ReaperGameSession::readSkillArray(
        std::uintptr_t progressAddress,
        std::uintptr_t& arrayAddress,
        std::int32_t& arrayLength,
        std::int32_t& xorKey,
        std::string& errorKey,
        DWORD& systemError) const
    {
        arrayAddress = 0;
        std::uintptr_t arrayObject{};
        std::uintptr_t skillData{};
        if (!memory_->readValue(progressAddress + 0x28, arrayObject, systemError) ||
            arrayObject == 0 ||
            !memory_->readValue(arrayObject + vectorDataOffset, skillData, systemError) ||
            skillData == 0 ||
            !memory_->readValue(arrayObject + vectorLengthOffset, arrayLength, systemError) ||
            arrayLength <= 0 || arrayLength > maximumSkillArrayLength ||
            !memory_->readValue(progressAddress + 0x78, xorKey, systemError))
        {
            errorKey = "ui.skill_data_not_ready";
            systemError = ERROR_INVALID_DATA;
            return false;
        }

        arrayAddress = skillData;
        return true;
    }

    bool ReaperGameSession::readSkills(
        std::vector<SkillInfo>& skills,
        std::string& errorKey,
        DWORD& systemError) const
    {
        skills.clear();
        std::uintptr_t progressAddress{};
        if (!findCurrentProgress(progressAddress, errorKey, systemError))
        {
            return false;
        }

        std::uintptr_t arrayAddress{};
        std::int32_t arrayLength{};
        std::int32_t xorKey{};
        if (!readSkillArray(progressAddress, arrayAddress, arrayLength, xorKey, errorKey, systemError))
        {
            return false;
        }

        DWORD scanError = ERROR_SUCCESS;
        const std::vector<std::uintptr_t> instances = memory_->findVtableInstances(
            processInfo_.moduleBase + skillVtableRva,
            maximumSkillInstances,
            scanError);
        if (scanError != ERROR_SUCCESS)
        {
            errorKey = "ui.skill_scan_failed";
            systemError = scanError;
            return false;
        }

        for (const std::uintptr_t skillAddress : instances)
        {
            SkillInfo skill;
            std::int32_t encodedLevel{};
            if (!memory_->readValue(skillAddress + 0x2C, skill.skillIndex, systemError) ||
                !memory_->readValue(skillAddress + 0x30, skill.skillId, systemError) ||
                !memory_->readValue(skillAddress + 0x28, skill.maximumLevel, systemError))
            {
                continue;
            }

            if (skill.skillIndex < 0 || skill.skillIndex >= arrayLength ||
                skill.skillId <= 0 || skill.maximumLevel <= 0)
            {
                continue;
            }

            if (!memory_->readValue(
                    arrayAddress + static_cast<std::uintptr_t>(skill.skillIndex) * sizeof(encodedLevel),
                    encodedLevel,
                    systemError))
            {
                continue;
            }

            skill.currentLevel = encodedLevel ^ xorKey;
            if (skill.currentLevel < 0 || skill.currentLevel > skill.maximumLevel)
            {
                continue;
            }
            skills.push_back(skill);
        }

        std::sort(skills.begin(), skills.end(),
            [](const SkillInfo& left, const SkillInfo& right)
            {
                return left.skillIndex < right.skillIndex;
            });
        if (skills.empty())
        {
            errorKey = "ui.skills_empty";
            systemError = ERROR_NOT_FOUND;
            return false;
        }
        return true;
    }

    bool ReaperGameSession::findSkill(
        std::int32_t skillId,
        SkillInfo& skill,
        std::string& errorKey,
        DWORD& systemError) const
    {
        std::vector<SkillInfo> skills;
        if (!readSkills(skills, errorKey, systemError))
        {
            return false;
        }

        const auto match = std::find_if(skills.begin(), skills.end(),
            [skillId](const SkillInfo& item) { return item.skillId == skillId; });
        if (match == skills.end())
        {
            errorKey = "ui.skill_not_found";
            systemError = ERROR_NOT_FOUND;
            return false;
        }

        skill = *match;
        return true;
    }

    bool ReaperGameSession::writeSkillLevel(
        const SkillInfo& skill,
        std::int32_t level,
        std::string& errorKey,
        DWORD& systemError)
    {
        if (level < 0 || level > skill.maximumLevel)
        {
            errorKey = "ui.skill_level_range";
            systemError = ERROR_INVALID_PARAMETER;
            return false;
        }
        if (!memory_->canWrite())
        {
            errorKey = "ui.read_only";
            systemError = ERROR_ACCESS_DENIED;
            return false;
        }

        std::uintptr_t progressAddress{};
        if (!findCurrentProgress(progressAddress, errorKey, systemError))
        {
            return false;
        }

        std::uintptr_t arrayAddress{};
        std::int32_t arrayLength{};
        std::int32_t xorKey{};
        if (!readSkillArray(progressAddress, arrayAddress, arrayLength, xorKey, errorKey, systemError) ||
            skill.skillIndex < 0 || skill.skillIndex >= arrayLength)
        {
            errorKey = "ui.skill_data_not_ready";
            systemError = ERROR_INVALID_DATA;
            return false;
        }

        const std::uintptr_t valueAddress =
            arrayAddress + static_cast<std::uintptr_t>(skill.skillIndex) * sizeof(std::int32_t);
        std::int32_t previousStored{};
        if (!memory_->readValue(valueAddress, previousStored, systemError))
        {
            errorKey = "ui.skill_data_not_ready";
            return false;
        }

        const std::int32_t encodedLevel = level ^ xorKey;
        if (!memory_->writeValue(valueAddress, encodedLevel, systemError))
        {
            errorKey = "ui.write_failed";
            return false;
        }

        std::int32_t actualStored{};
        if (!memory_->readValue(valueAddress, actualStored, systemError) ||
            actualStored != encodedLevel)
        {
            DWORD restoreError = ERROR_SUCCESS;
            const bool restored = memory_->writeValue(valueAddress, previousStored, restoreError);
            errorKey = restored ? "ui.write_verify_failed" : "ui.write_restore_failed";
            systemError = restored ? ERROR_INVALID_DATA : restoreError;
            return false;
        }
        return true;
    }

    bool ReaperGameSession::setSkillLevel(
        std::int32_t skillId,
        std::int32_t level,
        std::string& errorKey,
        DWORD& systemError)
    {
        SkillInfo skill;
        if (!findSkill(skillId, skill, errorKey, systemError))
        {
            return false;
        }
        return writeSkillLevel(skill, level, errorKey, systemError);
    }

    bool ReaperGameSession::replaceSkill(
        std::int32_t currentSkillId,
        std::int32_t replacementSkillId,
        std::string& errorKey,
        DWORD& systemError)
    {
        if (currentSkillId == replacementSkillId)
        {
            errorKey = "ui.skill_same";
            systemError = ERROR_INVALID_PARAMETER;
            return false;
        }

        SkillInfo currentSkill;
        SkillInfo replacementSkill;
        if (!findSkill(currentSkillId, currentSkill, errorKey, systemError) ||
            !findSkill(replacementSkillId, replacementSkill, errorKey, systemError))
        {
            return false;
        }
        if (currentSkill.currentLevel == 0)
        {
            errorKey = "ui.skill_not_learned";
            systemError = ERROR_INVALID_PARAMETER;
            return false;
        }
        if (replacementSkill.currentLevel != 0)
        {
            errorKey = "ui.skill_already_learned";
            systemError = ERROR_INVALID_PARAMETER;
            return false;
        }
        if (replacementSkill.maximumLevel < currentSkill.currentLevel)
        {
            errorKey = "ui.skill_level_range";
            systemError = ERROR_INVALID_PARAMETER;
            return false;
        }

        if (!writeSkillLevel(replacementSkill, currentSkill.currentLevel, errorKey, systemError))
        {
            return false;
        }
        if (!writeSkillLevel(currentSkill, 0, errorKey, systemError))
        {
            std::string restoreKey;
            DWORD restoreError = ERROR_SUCCESS;
            if (!writeSkillLevel(replacementSkill, 0, restoreKey, restoreError))
            {
                errorKey = "ui.write_restore_failed";
                systemError = restoreError;
            }
            return false;
        }
        return true;
    }
}
