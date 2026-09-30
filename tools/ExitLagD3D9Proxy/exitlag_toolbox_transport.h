#pragma once
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>

struct ExitLagToolboxRecord {
    DWORD magic;
    DWORD schema;
    volatile LONG state;
    DWORD process_id;
    ULONGLONG creation_time;
    volatile LONG sequence;
    char tcp_address[64];
    char udp_address[64];
};
static_assert(sizeof(ExitLagToolboxRecord) == 160, "Transport ABI size mismatch");
constexpr DWORD kToolboxTransportMagic = 0x50545831;

inline std::wstring ToolboxMappingName(DWORD pid) {
    return L"Local\\PlayTera.ExitLag.Toolbox." + std::to_wstring(pid);
}

inline bool IsToolboxLoopback(const char* address) {
    if (!address || std::strlen(address) >= 64) return false;
    unsigned first = 0, second = 0, third = 0, fourth = 0, port = 0;
    char trailing = 0;
    return sscanf_s(address, "%u.%u.%u.%u:%u%c", &first, &second, &third, &fourth, &port, &trailing, 1u) == 5
        && first == 127 && second <= 255 && third <= 255 && fourth <= 255 && port > 0 && port <= 65535;
}

inline ULONGLONG ToolboxProcessCreation(HANDLE process) {
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(process, &creation, &exit, &kernel, &user)) return 0;
    return (static_cast<ULONGLONG>(creation.dwHighDateTime) << 32) | creation.dwLowDateTime;
}

class ExitLagToolboxPublisher {
    HANDLE mapping_ = nullptr;
    ExitLagToolboxRecord* record_ = nullptr;
public:
    ExitLagToolboxPublisher() = default;
    ExitLagToolboxPublisher(const ExitLagToolboxPublisher&) = delete;
    ExitLagToolboxPublisher& operator=(const ExitLagToolboxPublisher&) = delete;
    ~ExitLagToolboxPublisher() {
        Stop();
        if (record_) UnmapViewOfFile(record_);
        if (mapping_) CloseHandle(mapping_);
    }
    bool Publish(const char* tcp, const char* udp) {
        if (!IsToolboxLoopback(tcp) || !IsToolboxLoopback(udp)) {
            SetLastError(ERROR_INVALID_ADDRESS);
            Stop();
            return false;
        }
        if (!mapping_) {
            const ULONGLONG creation = ToolboxProcessCreation(GetCurrentProcess());
            if (!creation) return false;
            mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                sizeof(ExitLagToolboxRecord), ToolboxMappingName(GetCurrentProcessId()).c_str());
            if (!mapping_) return false;
            if (GetLastError() == ERROR_ALREADY_EXISTS) {
                CloseHandle(mapping_);
                mapping_ = nullptr;
                SetLastError(ERROR_ALREADY_EXISTS);
                return false;
            }
            record_ = static_cast<ExitLagToolboxRecord*>(MapViewOfFile(mapping_, FILE_MAP_WRITE, 0, 0, sizeof(ExitLagToolboxRecord)));
            if (!record_) {
                const DWORD error = GetLastError();
                CloseHandle(mapping_);
                mapping_ = nullptr;
                SetLastError(error);
                return false;
            }
            ZeroMemory(record_, sizeof(*record_));
            record_->magic = kToolboxTransportMagic;
            record_->schema = 1;
            record_->process_id = GetCurrentProcessId();
            record_->creation_time = creation;
        }
        InterlockedExchange(&record_->state, 0);
        InterlockedIncrement(&record_->sequence);
        std::memset(record_->tcp_address, 0, sizeof(record_->tcp_address));
        std::memset(record_->udp_address, 0, sizeof(record_->udp_address));
        std::memcpy(record_->tcp_address, tcp, std::strlen(tcp));
        std::memcpy(record_->udp_address, udp, std::strlen(udp));
        InterlockedIncrement(&record_->sequence);
        InterlockedExchange(&record_->state, 1);
        return true;
    }
    void Stop() {
        if (record_) InterlockedExchange(&record_->state, 0);
    }
};
