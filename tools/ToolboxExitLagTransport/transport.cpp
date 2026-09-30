#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "../ExitLagD3D9Proxy/exitlag_toolbox_transport.h"

static void Error(const char* domain, DWORD code) {
    std::fprintf(stderr, "{\"event\":\"error\",\"domain\":\"%s\",\"code\":%lu}\n", domain, code);
    std::fflush(stderr);
}

static bool SameUser(HANDLE process) {
    HANDLE currentToken = nullptr, gameToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &currentToken)) return false;
    if (!OpenProcessToken(process, TOKEN_QUERY, &gameToken)) { CloseHandle(currentToken); return false; }
    DWORD currentSize = 0, gameSize = 0;
    GetTokenInformation(currentToken, TokenUser, nullptr, 0, &currentSize);
    GetTokenInformation(gameToken, TokenUser, nullptr, 0, &gameSize);
    std::vector<BYTE> current(currentSize), game(gameSize);
    const bool success = currentSize && gameSize
        && GetTokenInformation(currentToken, TokenUser, current.data(), currentSize, &currentSize)
        && GetTokenInformation(gameToken, TokenUser, game.data(), gameSize, &gameSize)
        && EqualSid(reinterpret_cast<TOKEN_USER*>(current.data())->User.Sid,
                    reinterpret_cast<TOKEN_USER*>(game.data())->User.Sid);
    CloseHandle(currentToken);
    CloseHandle(gameToken);
    return success;
}

static bool VerifySdk(const std::wstring& file) {
    HANDLE input = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (input == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION information{};
    const bool regular = GetFileInformationByHandle(input, &information)
        && !(information.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))
        && information.nFileSizeHigh == 0 && information.nFileSizeLow == 8207360;
    if (!regular) { CloseHandle(input); return false; }
    std::vector<BYTE> bytes(information.nFileSizeLow);
    DWORD read = 0;
    const bool loaded = ReadFile(input, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr)
        && read == bytes.size();
    CloseHandle(input);
    if (!loaded) return false;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BYTE digest[32]{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return false;
    const bool hashed = BCryptHash(algorithm, nullptr, 0, bytes.data(), static_cast<ULONG>(bytes.size()), digest, sizeof(digest)) >= 0;
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!hashed) return false;
    char hex[65]{};
    for (unsigned index = 0; index < sizeof(digest); index++) std::snprintf(hex + index * 2, 3, "%02x", digest[index]);
    return std::strcmp(hex, "c62aa3b23c1da2fc8c3c1587771ea71a56b4ee939f82e43ff0b8f2e94b7ce6ca") == 0;
}

static bool ReadRecord(const ExitLagToolboxRecord* view, DWORD pid, ULONGLONG creation, ExitLagToolboxRecord& snapshot) {
    const LONG sequence = view->sequence;
    if (sequence <= 0 || (sequence & 1) || view->state != 1) return false;
    MemoryBarrier();
    std::memcpy(&snapshot, view, sizeof(snapshot));
    MemoryBarrier();
    return sequence == view->sequence && view->state == 1
        && snapshot.magic == kToolboxTransportMagic && snapshot.schema == 1
        && snapshot.process_id == pid && snapshot.creation_time == creation
        && std::memchr(snapshot.tcp_address, 0, sizeof(snapshot.tcp_address))
        && std::memchr(snapshot.udp_address, 0, sizeof(snapshot.udp_address))
        && IsToolboxLoopback(snapshot.tcp_address) && IsToolboxLoopback(snapshot.udp_address);
}

struct InputPump {
    SOCKET socket;
    HANDLE input;
};

static DWORD WINAPI PumpInput(void* argument) {
    auto* pump = static_cast<InputPump*>(argument);
    char buffer[65536]{};
    DWORD count = 0;
    while (ReadFile(pump->input, buffer, sizeof(buffer), &count, nullptr) && count) {
        DWORD offset = 0;
        while (offset < count) {
            const int sent = send(pump->socket, buffer + offset, static_cast<int>(count - offset), 0);
            if (sent <= 0) { shutdown(pump->socket, SD_BOTH); return 0; }
            offset += static_cast<DWORD>(sent);
        }
    }
    shutdown(pump->socket, SD_SEND);
    return 0;
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) { Error("arguments", ERROR_INVALID_PARAMETER); return 64; }
    wchar_t* end = nullptr;
    const unsigned long parsed = std::wcstoul(argv[1], &end, 10);
    if (!parsed || !end || *end || argv[1][0] == L'-') { Error("pid", ERROR_INVALID_PARAMETER); return 64; }
    const DWORD pid = static_cast<DWORD>(parsed);
    HANDLE game = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
    if (!game) { Error("game", GetLastError()); return 1; }
    const ULONGLONG creation = ToolboxProcessCreation(game);
    wchar_t image[32768]{};
    DWORD imageSize = _countof(image);
    if (!creation || !SameUser(game) || !QueryFullProcessImageNameW(game, 0, image, &imageSize)) {
        Error("game_identity", ERROR_ACCESS_DENIED); CloseHandle(game); return 1;
    }
    std::wstring imagePath(image, imageSize);
    const size_t separator = imagePath.find_last_of(L"\\/");
    if (separator == std::wstring::npos) { Error("game_identity", ERROR_INVALID_NAME); CloseHandle(game); return 1; }
    const std::wstring name = imagePath.substr(separator + 1);
    if (_wcsicmp(name.c_str(), L"cabalmain.exe") && _wcsicmp(name.c_str(), L"TERA.exe") && _wcsicmp(name.c_str(), L"VTEQ.exe")) {
        Error("game_identity", ERROR_INVALID_NAME); CloseHandle(game); return 1;
    }
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, ToolboxMappingName(pid).c_str());
    if (!mapping) { Error("transport_missing", GetLastError()); CloseHandle(game); return 2; }
    auto* view = static_cast<const ExitLagToolboxRecord*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(ExitLagToolboxRecord)));
    ExitLagToolboxRecord snapshot{};
    if (!view || !ReadRecord(view, pid, creation, snapshot)) {
        Error("transport_not_ready", ERROR_INVALID_DATA);
        if (view) UnmapViewOfFile(view);
        CloseHandle(mapping); CloseHandle(game); return 2;
    }
    const std::wstring sdkPath = imagePath.substr(0, separator + 1) + L"exitlag.dll";
    if (!VerifySdk(sdkPath)) {
        Error("sdk_integrity", ERROR_INVALID_DATA); UnmapViewOfFile(view); CloseHandle(mapping); CloseHandle(game); return 3;
    }
    WSADATA startup{};
    const int started = WSAStartup(MAKEWORD(2, 2), &startup);
    if (started) { Error("winsock", static_cast<DWORD>(started)); return 4; }
    HMODULE sdk = LoadLibraryExW(sdkPath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!sdk) { Error("sdk_load", GetLastError()); return 4; }
    using Install = LONG (__cdecl*)(const char*, const char*);
    using Uninstall = LONG (__cdecl*)();
    auto install = reinterpret_cast<Install>(GetProcAddress(sdk, "exitlag_install_hooks_with_listen_addresses"));
    auto uninstall = reinterpret_cast<Uninstall>(GetProcAddress(sdk, "exitlag_uninstall_hooks"));
    if (!install || !uninstall) { Error("sdk_exports", ERROR_PROC_NOT_FOUND); return 4; }
    const LONG installed = install(snapshot.tcp_address, snapshot.udp_address);
    if (installed) { Error("sdk_install", static_cast<DWORD>(installed)); uninstall(); return 5; }
    const std::string initialTcp(snapshot.tcp_address), initialUdp(snapshot.udp_address);
    SOCKET outgoing = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (outgoing == INVALID_SOCKET) { Error("socket", static_cast<DWORD>(WSAGetLastError())); uninstall(); return 5; }
    const DWORD timeoutMs = 5000;
    setsockopt(outgoing, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
    const BOOL noDelay = TRUE;
    setsockopt(outgoing, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
    u_long nonblocking = 1;
    ioctlsocket(outgoing, FIONBIO, &nonblocking);
    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_addr.s_addr = htonl(0x0102030e);
    target.sin_port = htons(38101);
    const int connectionResult = connect(outgoing, reinterpret_cast<sockaddr*>(&target), sizeof(target));
    int connectionError = connectionResult == 0 ? 0 : WSAGetLastError();
    if (connectionError == WSAEWOULDBLOCK) {
        fd_set writable;
        FD_ZERO(&writable); FD_SET(outgoing, &writable);
        timeval wait{5, 0};
        const int selected = select(0, nullptr, &writable, nullptr, &wait);
        int errorSize = sizeof(connectionError);
        if (selected > 0) getsockopt(outgoing, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&connectionError), &errorSize);
        else connectionError = selected == 0 ? WSAETIMEDOUT : WSAGetLastError();
    }
    nonblocking = 0;
    ioctlsocket(outgoing, FIONBIO, &nonblocking);
    if (connectionError || !ReadRecord(view, pid, creation, snapshot)
        || initialTcp != snapshot.tcp_address || initialUdp != snapshot.udp_address
        || WaitForSingleObject(game, 0) != WAIT_TIMEOUT) {
        Error("connect", connectionError ? static_cast<DWORD>(connectionError) : ERROR_OPERATION_ABORTED);
        closesocket(outgoing); uninstall(); return 6;
    }
    InputPump pump{outgoing, GetStdHandle(STD_INPUT_HANDLE)};
    HANDLE inputThread = CreateThread(nullptr, 0, PumpInput, &pump, 0, nullptr);
    if (!inputThread) { Error("input_thread", GetLastError()); closesocket(outgoing); uninstall(); return 7; }
    std::fprintf(stderr, "{\"event\":\"connected\"}\n");
    std::fflush(stderr);
    bool ioFailed = false;
    char buffer[65536]{};
    while (WaitForSingleObject(game, 0) == WAIT_TIMEOUT && ReadRecord(view, pid, creation, snapshot)) {
        if (initialTcp != snapshot.tcp_address || initialUdp != snapshot.udp_address) {
            Error("transport_changed", ERROR_OPERATION_ABORTED); ioFailed = true; break;
        }
        fd_set readable;
        FD_ZERO(&readable); FD_SET(outgoing, &readable);
        timeval wait{0, 250000};
        const int selected = select(0, &readable, nullptr, nullptr, &wait);
        if (selected < 0) { Error("receive", static_cast<DWORD>(WSAGetLastError())); ioFailed = true; break; }
        if (!selected) continue;
        const int count = recv(outgoing, buffer, sizeof(buffer), 0);
        if (count <= 0) {
            if (count < 0) { Error("receive", static_cast<DWORD>(WSAGetLastError())); ioFailed = true; }
            break;
        }
        DWORD offset = 0;
        while (offset < static_cast<DWORD>(count)) {
            DWORD written = 0;
            if (!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buffer + offset, static_cast<DWORD>(count) - offset, &written, nullptr) || !written) {
                ioFailed = true; break;
            }
            offset += written;
        }
        if (ioFailed) break;
    }
    shutdown(outgoing, SD_BOTH);
    CancelSynchronousIo(inputThread);
    const DWORD stopped = WaitForSingleObject(inputThread, 5000);
    if (stopped != WAIT_OBJECT_0) { Error("input_cleanup", ERROR_TIMEOUT); return 8; }
    CloseHandle(inputThread);
    closesocket(outgoing);
    const LONG removed = uninstall();
    if (removed) { Error("sdk_uninstall", static_cast<DWORD>(removed)); ioFailed = true; }
    UnmapViewOfFile(view); CloseHandle(mapping); CloseHandle(game);
    WSACleanup();
    std::fprintf(stderr, "{\"event\":\"closed\"}\n");
    std::fflush(stderr);
    return ioFailed ? 9 : 0;
}
