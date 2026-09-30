#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>

static bool TrustedRuntime(const std::wstring& file) {
    HANDLE input = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (input == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION information{};
    const bool regular = GetFileInformationByHandle(input, &information)
        && !(information.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))
        && information.nFileSizeHigh == 0 && information.nFileSizeLow <= 200 * 1024 * 1024;
    if (!regular) { CloseHandle(input); return false; }
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) { CloseHandle(input); return false; }
    DWORD objectLength = 0, copied = 0;
    bool success = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<BYTE*>(&objectLength), sizeof(objectLength), &copied, 0) >= 0;
    std::vector<BYTE> object(objectLength), buffer(65536);
    if (success) success = BCryptCreateHash(algorithm, &hash, object.data(), objectLength, nullptr, 0, 0) >= 0;
    DWORD read = 0;
    while (success) {
        if (!ReadFile(input, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) { success = false; break; }
        if (!read) break;
        success = BCryptHashData(hash, buffer.data(), read, 0) >= 0;
    }
    BYTE digest[32]{};
    if (success) success = BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0); CloseHandle(input);
    char hex[65]{};
    for (unsigned index = 0; index < sizeof(digest); index++) std::snprintf(hex + index * 2, 3, "%02x", digest[index]);
    return success && !std::strcmp(hex, "543519f53189a221698264eeb6fc4a5513a751bfe9168aa691c824c831069d89");
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, wchar_t* command, int) {
    wchar_t image[32768]{};
    const DWORD count = GetModuleFileNameW(nullptr, image, _countof(image));
    if (!count || count >= _countof(image)) return 1;
    std::wstring root(image, count);
    root.resize(root.find_last_of(L'\\'));
    const std::wstring runtime = root + L"\\node_modules\\electron\\dist\\electron.exe";
    const bool verifyOnly = !wcscmp(command, L"--verify-only");
    if (!TrustedRuntime(runtime)) {
        if (!verifyOnly) MessageBoxW(nullptr, L"Die Toolbox-Laufzeit fehlt oder stimmt nicht mit dem PlayTera-Paket ueberein. Bitte das freigegebene PlayTera-Toolbox-Setup erneut verwenden.", L"PlayTera Toolbox", MB_OK | MB_ICONERROR);
        return 2;
    }
    if (verifyOnly) return 0;
    SetEnvironmentVariableW(L"ELECTRON_RUN_AS_NODE", nullptr);
    const std::wstring arguments = L"\"" + runtime + L"\" \"" + root + L"\\bin\\index-gui.js\"";
    std::vector<wchar_t> mutableArguments(arguments.begin(), arguments.end()); mutableArguments.push_back(0);
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(runtime.c_str(), mutableArguments.data(), nullptr, nullptr, FALSE, 0, nullptr, root.c_str(), &startup, &process)) {
        MessageBoxW(nullptr, L"PlayTera Toolbox konnte nicht gestartet werden. Bitte Ausfuehrungsrechte und das vollstaendige Paket pruefen.", L"PlayTera Toolbox", MB_OK | MB_ICONERROR);
        return 3;
    }
    CloseHandle(process.hThread); CloseHandle(process.hProcess); return 0;
}
