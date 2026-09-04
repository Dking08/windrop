#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>

// ----------------------------------------------------------------------------
// Utility helpers for windrop.exe
// ----------------------------------------------------------------------------
namespace Utils
{
    inline constexpr wchar_t kVersion[] = L"2.1.0";

    // -----------------------------------------------------------------------
    // ResolvePaths
    //
    // Accepts a list of raw arguments (may be relative, absolute, or glob
    // patterns such as "*.png").  Returns fully-qualified, normalised
    // absolute paths for every file that exists.
    //
    // missing - populated with any argument that matched no files.
    // -----------------------------------------------------------------------
    std::vector<std::wstring> ResolvePaths(
        const std::vector<std::wstring>& args,
        std::vector<std::wstring>&       missing);

    // -----------------------------------------------------------------------
    // ParseCommandLine
    //
    // Splits the process command line (skipping argv[0]) into individual
    // tokens, respecting quoted arguments.
    // Prefer using wmain(argc, argv) instead if the CRT provides it.
    // -----------------------------------------------------------------------
    std::vector<std::wstring> ParseCommandLine(const wchar_t* cmdLine);

    // -----------------------------------------------------------------------
    // CLI feedback helpers
    // -----------------------------------------------------------------------
    void PrintVersion();
    void PrintUsage(FILE* out = stdout);
    void PrintError(const std::wstring& msg);
}
