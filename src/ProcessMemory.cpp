#include "ReaperTrainer/ProcessMemory.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <utility>

namespace ReaperTrainer
{
    namespace
    {
        bool isReadableProtection(DWORD protection)
        {
            if ((protection & PAGE_GUARD) != 0)
            {
                return false;
            }

            switch (protection & 0xFF)
            {
            case PAGE_READONLY:
            case PAGE_READWRITE:
            case PAGE_WRITECOPY:
            case PAGE_EXECUTE_READ:
            case PAGE_EXECUTE_READWRITE:
            case PAGE_EXECUTE_WRITECOPY:
                return true;
            default:
                return false;
            }
        }
    }

    ProcessMemory::ProcessMemory(HANDLE processHandle, DWORD processId, bool canWrite) noexcept
        : processHandle_(processHandle),
          processId_(processId),
          canWrite_(canWrite)
    {
    }

    ProcessMemory::ProcessMemory(ProcessMemory&& other) noexcept
        : processHandle_(std::exchange(other.processHandle_, nullptr)),
          processId_(std::exchange(other.processId_, 0)),
          canWrite_(std::exchange(other.canWrite_, false))
    {
    }

    ProcessMemory& ProcessMemory::operator=(ProcessMemory&& other) noexcept
    {
        if (this != &other)
        {
            close();
            processHandle_ = std::exchange(other.processHandle_, nullptr);
            processId_ = std::exchange(other.processId_, 0);
            canWrite_ = std::exchange(other.canWrite_, false);
        }
        return *this;
    }

    ProcessMemory::~ProcessMemory()
    {
        close();
    }

    std::optional<ProcessMemory> ProcessMemory::open(
        DWORD processId,
        bool requestWriteAccess,
        DWORD& systemError)
    {
        systemError = ERROR_SUCCESS;
        DWORD access = PROCESS_QUERY_INFORMATION | PROCESS_VM_READ;
        if (requestWriteAccess)
        {
            access |= PROCESS_VM_WRITE | PROCESS_VM_OPERATION;
        }

        HANDLE processHandle = OpenProcess(access, FALSE, processId);
        if (processHandle == nullptr)
        {
            systemError = GetLastError();
            return std::nullopt;
        }

        return ProcessMemory(processHandle, processId, requestWriteAccess);
    }

    bool ProcessMemory::isOpen() const noexcept
    {
        return processHandle_ != nullptr;
    }

    bool ProcessMemory::isRunning() const noexcept
    {
        DWORD exitCode{};
        return isOpen() &&
            GetExitCodeProcess(processHandle_, &exitCode) != FALSE &&
            exitCode == STILL_ACTIVE;
    }

    bool ProcessMemory::canWrite() const noexcept
    {
        return canWrite_;
    }

    DWORD ProcessMemory::processId() const noexcept
    {
        return processId_;
    }

    bool ProcessMemory::readBytes(
        std::uintptr_t address,
        void* destination,
        std::size_t size,
        DWORD& systemError) const
    {
        systemError = ERROR_SUCCESS;
        if (!isOpen() || destination == nullptr || size == 0)
        {
            systemError = ERROR_INVALID_PARAMETER;
            return false;
        }

        SIZE_T bytesRead = 0;
        const BOOL succeeded = ReadProcessMemory(
            processHandle_,
            reinterpret_cast<LPCVOID>(address),
            destination,
            size,
            &bytesRead);
        if (!succeeded || bytesRead != size)
        {
            systemError = GetLastError();
            if (systemError == ERROR_SUCCESS)
            {
                systemError = ERROR_PARTIAL_COPY;
            }
            return false;
        }
        return true;
    }

    bool ProcessMemory::writeBytes(
        std::uintptr_t address,
        const void* source,
        std::size_t size,
        DWORD& systemError) const
    {
        systemError = ERROR_SUCCESS;
        if (!isOpen() || !canWrite_ || source == nullptr || size == 0)
        {
            systemError = !canWrite_ ? ERROR_ACCESS_DENIED : ERROR_INVALID_PARAMETER;
            return false;
        }

        SIZE_T bytesWritten = 0;
        const BOOL succeeded = WriteProcessMemory(
            processHandle_,
            reinterpret_cast<LPVOID>(address),
            source,
            size,
            &bytesWritten);
        if (!succeeded || bytesWritten != size)
        {
            systemError = GetLastError();
            if (systemError == ERROR_SUCCESS)
            {
                systemError = ERROR_PARTIAL_COPY;
            }
            return false;
        }
        return true;
    }

    std::vector<std::uintptr_t> ProcessMemory::findVtableInstances(
        std::uintptr_t vtableAddress,
        std::size_t maximumInstances,
        DWORD& systemError) const
    {
        systemError = ERROR_SUCCESS;
        std::vector<std::uintptr_t> matches;
        if (!isOpen() || maximumInstances == 0)
        {
            systemError = ERROR_INVALID_PARAMETER;
            return matches;
        }

        SYSTEM_INFO systemInfo{};
        GetNativeSystemInfo(&systemInfo);
        std::uintptr_t address =
            reinterpret_cast<std::uintptr_t>(systemInfo.lpMinimumApplicationAddress);
        const std::uintptr_t maximumAddress =
            reinterpret_cast<std::uintptr_t>(systemInfo.lpMaximumApplicationAddress);
        constexpr std::size_t chunkSize = 1024 * 1024;
        std::vector<std::byte> buffer(chunkSize);

        while (address < maximumAddress)
        {
            MEMORY_BASIC_INFORMATION information{};
            const SIZE_T querySize = VirtualQueryEx(
                processHandle_,
                reinterpret_cast<LPCVOID>(address),
                &information,
                sizeof(information));
            if (querySize == 0)
            {
                break;
            }

            const auto regionBase = reinterpret_cast<std::uintptr_t>(information.BaseAddress);
            const auto regionSize = static_cast<std::uintptr_t>(information.RegionSize);
            if (regionSize == 0 || regionBase > std::numeric_limits<std::uintptr_t>::max() - regionSize)
            {
                break;
            }

            if (information.State == MEM_COMMIT &&
                information.Type == MEM_PRIVATE &&
                isReadableProtection(information.Protect))
            {
                const std::uintptr_t regionEnd = regionBase + regionSize;
                for (std::uintptr_t position = regionBase; position < regionEnd;)
                {
                    const std::size_t requestSize = static_cast<std::size_t>(
                        std::min<std::uintptr_t>(chunkSize, regionEnd - position));
                    SIZE_T bytesRead = 0;
                    if (ReadProcessMemory(
                            processHandle_,
                            reinterpret_cast<LPCVOID>(position),
                            buffer.data(),
                            requestSize,
                            &bytesRead) &&
                        bytesRead >= sizeof(std::uintptr_t))
                    {
                        const std::size_t readableSize = static_cast<std::size_t>(bytesRead);
                        for (std::size_t offset = 0;
                             offset + sizeof(std::uintptr_t) <= readableSize;
                             offset += sizeof(std::uintptr_t))
                        {
                            std::uintptr_t value{};
                            std::memcpy(&value, buffer.data() + offset, sizeof(value));
                            if (value == vtableAddress)
                            {
                                matches.push_back(position + offset);
                                if (matches.size() >= maximumInstances)
                                {
                                    return matches;
                                }
                            }
                        }
                    }

                    if (requestSize == 0)
                    {
                        break;
                    }
                    position += requestSize;
                }
            }

            const std::uintptr_t nextAddress = regionBase + regionSize;
            if (nextAddress <= address)
            {
                break;
            }
            address = nextAddress;
        }

        return matches;
    }

    void ProcessMemory::close() noexcept
    {
        if (processHandle_ != nullptr)
        {
            CloseHandle(processHandle_);
            processHandle_ = nullptr;
        }
        processId_ = 0;
        canWrite_ = false;
    }
}
