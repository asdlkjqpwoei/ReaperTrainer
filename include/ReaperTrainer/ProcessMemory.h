#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace ReaperTrainer
{
    class ProcessMemory
    {
    public:
        ProcessMemory() = default;
        ProcessMemory(const ProcessMemory&) = delete;
        ProcessMemory& operator=(const ProcessMemory&) = delete;
        ProcessMemory(ProcessMemory&& other) noexcept;
        ProcessMemory& operator=(ProcessMemory&& other) noexcept;
        ~ProcessMemory();

        static std::optional<ProcessMemory> open(
            DWORD processId,
            bool requestWriteAccess,
            DWORD& systemError);

        bool isOpen() const noexcept;
        bool isRunning() const noexcept;
        bool canWrite() const noexcept;
        DWORD processId() const noexcept;

        bool readBytes(
            std::uintptr_t address,
            void* destination,
            std::size_t size,
            DWORD& systemError) const;

        bool writeBytes(
            std::uintptr_t address,
            const void* source,
            std::size_t size,
            DWORD& systemError) const;

        template <typename Value>
        bool readValue(std::uintptr_t address, Value& value, DWORD& systemError) const
        {
            static_assert(std::is_trivially_copyable_v<Value>);
            return readBytes(address, &value, sizeof(Value), systemError);
        }

        template <typename Value>
        bool writeValue(std::uintptr_t address, const Value& value, DWORD& systemError) const
        {
            static_assert(std::is_trivially_copyable_v<Value>);
            return writeBytes(address, &value, sizeof(Value), systemError);
        }

        std::vector<std::uintptr_t> findVtableInstances(
            std::uintptr_t vtableAddress,
            std::size_t maximumInstances,
            DWORD& systemError) const;

    private:
        ProcessMemory(HANDLE processHandle, DWORD processId, bool canWrite) noexcept;
        void close() noexcept;

        HANDLE processHandle_{};
        DWORD processId_{};
        bool canWrite_{};
    };
}
