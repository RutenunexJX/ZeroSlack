#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {
std::filesystem::path executablePath()
{
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size())
        return {};
    buffer.resize(length);
    return std::filesystem::path(buffer);
}

std::wstring forwardSlashes(std::wstring value)
{
    for (wchar_t& character : value) {
        if (character == L'\\')
            character = L'/';
    }
    return value;
}

std::wstring environmentValue(const wchar_t* name)
{
    const DWORD length = GetEnvironmentVariableW(name, nullptr, 0);
    if (length == 0)
        return {};
    std::wstring value(length, L'\0');
    const DWORD copied = GetEnvironmentVariableW(
        name, value.data(), static_cast<DWORD>(value.size()));
    if (copied == 0 || copied >= value.size())
        return {};
    value.resize(copied);
    return value;
}

bool requestsVersion(int argc, wchar_t** argv)
{
    for (int index = 1; index < argc; ++index) {
        if (std::wstring(argv[index]) == L"--version")
            return true;
    }
    return false;
}

bool generatesCppModel(int argc, wchar_t** argv)
{
    for (int index = 1; index < argc; ++index) {
        const std::wstring argument(argv[index]);
        if (argument == L"--cc" || argument == L"--binary"
            || argument == L"--sc") {
            return true;
        }
    }
    return false;
}

bool hasTimeContextFlag(int argc, wchar_t** argv)
{
    for (int index = 1; index < argc; ++index) {
        if (std::wstring(argv[index]).find(L"VL_TIME_CONTEXT")
            != std::wstring::npos) {
            return true;
        }
    }
    return false;
}

std::wstring quoteWindowsArgument(const std::wstring& argument)
{
    if (!argument.empty()
        && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        return argument;
    }

    std::wstring quoted = L"\"";
    size_t backslashes = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'\"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(character);
            backslashes = 0;
            continue;
        }
        quoted.append(backslashes, L'\\');
        backslashes = 0;
        quoted.push_back(character);
    }
    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'\"');
    return quoted;
}
}

int wmain(int argc, wchar_t** argv)
{
    const std::filesystem::path launcher = executablePath();
    if (launcher.empty()) {
        std::wcerr << L"Cannot resolve the portable Verilator launcher path.\n";
        return 127;
    }

    const std::filesystem::path binDirectory = launcher.parent_path();
    const std::filesystem::path verilatorRoot = binDirectory.parent_path();
    const std::filesystem::path toolchainRoot = verilatorRoot.parent_path();
    const std::filesystem::path compilerBin = toolchainRoot / L"mingw" / L"bin";
    const std::filesystem::path verilatorBinary =
        binDirectory / L"verilator_bin.exe";

    if (requestsVersion(argc, argv)) {
        std::ifstream versionFile(binDirectory / L"verilator-version.txt");
        std::string version;
        std::getline(versionFile, version);
        if (version.empty()) {
            std::cerr << "Portable Verilator version metadata is missing.\n";
            return 127;
        }
        std::cout << "Verilator " << version << '\n';
        return 0;
    }

    _wputenv_s(L"VERILATOR_ROOT",
               forwardSlashes(verilatorRoot.wstring()).c_str());
    _wputenv_s(L"MAKE", L"mingw32-make.exe");
    std::wstring path = compilerBin.wstring() + L";"
        + binDirectory.wstring();
    const std::wstring inheritedPath = environmentValue(L"PATH");
    if (!inheritedPath.empty())
        path += L";" + inheritedPath;
    _wputenv_s(L"PATH", path.c_str());

    const std::wstring binary = verilatorBinary.wstring();
    std::wstring commandLine = quoteWindowsArgument(binary);
    for (int index = 1; index < argc; ++index) {
        commandLine.push_back(L' ');
        commandLine += quoteWindowsArgument(argv[index]);
    }
    if (generatesCppModel(argc, argv) && !hasTimeContextFlag(argc, argv)) {
        commandLine += L" -CFLAGS -DVL_TIME_CONTEXT";
    }

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};
    if (!CreateProcessW(binary.c_str(), commandLine.data(), nullptr, nullptr,
                        TRUE, 0, nullptr, nullptr, &startupInfo,
                        &processInfo)) {
        std::wcerr << L"Cannot start portable verilator_bin.exe.\n";
        return 127;
    }
    WaitForSingleObject(processInfo.hProcess, INFINITE);
    DWORD exitCode = 127;
    GetExitCodeProcess(processInfo.hProcess, &exitCode);
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return static_cast<int>(exitCode);
}
