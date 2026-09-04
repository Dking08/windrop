#include "Utils.h"
#include <cstdio>
#include <cstring>
#include <algorithm>

namespace Utils
{

// ---------------------------------------------------------------------------
// ResolvePaths
//
// For each argument:
//   1. If it contains a wildcard character ('*' or '?'), expand via
//      FindFirstFileW / FindNextFileW in the directory portion.
//   2. Otherwise, canonicalise with GetFullPathNameW and check existence.
//
// All returned paths are absolute and verified to exist.
// ---------------------------------------------------------------------------
std::vector<std::wstring> ResolvePaths(
    const std::vector<std::wstring>& args,
    std::vector<std::wstring>&       missing)
{
    std::vector<std::wstring> result;

    for (const auto& arg : args)
    {
        bool hasWildcard = (arg.find(L'*') != std::wstring::npos ||
                            arg.find(L'?') != std::wstring::npos);

        if (hasWildcard)
        {
            // ------------------------------------------------------------------
            // Wildcard expansion
            // Split into directory prefix and pattern, then enumerate.
            // ------------------------------------------------------------------

            // Get the full path of the pattern (expands relative dirs)
            wchar_t fullPattern[MAX_PATH * 2] = {};
            if (!GetFullPathNameW(arg.c_str(), MAX_PATH * 2, fullPattern, nullptr))
            {
                missing.push_back(arg);
                continue;
            }

            // Determine the directory portion
            std::wstring patternStr(fullPattern);
            size_t lastSlash = patternStr.find_last_of(L"\\/");
            std::wstring dir = (lastSlash != std::wstring::npos)
                                ? patternStr.substr(0, lastSlash + 1)
                                : L".\\";

            WIN32_FIND_DATAW fd = {};
            HANDLE hFind = FindFirstFileW(fullPattern, &fd);
            bool found = false;

            if (hFind != INVALID_HANDLE_VALUE)
            {
                do
                {
                    // Skip directories and special entries
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                    if (wcscmp(fd.cFileName, L".") == 0)  continue;
                    if (wcscmp(fd.cFileName, L"..") == 0) continue;

                    std::wstring fullPath = dir + fd.cFileName;
                    result.push_back(fullPath);
                    found = true;

                } while (FindNextFileW(hFind, &fd));

                FindClose(hFind);
            }

            if (!found)
                missing.push_back(arg);
        }
        else
        {
            // ------------------------------------------------------------------
            // Non-wildcard: canonicalise and verify
            // ------------------------------------------------------------------
            wchar_t fullPath[MAX_PATH * 2] = {};
            DWORD len = GetFullPathNameW(arg.c_str(), MAX_PATH * 2, fullPath, nullptr);
            if (len == 0 || len >= MAX_PATH * 2)
            {
                missing.push_back(arg);
                continue;
            }

            // GetFileAttributesW is the lightest existence check
            DWORD attrs = GetFileAttributesW(fullPath);
            if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY))
            {
                missing.push_back(arg);
                continue;
            }

            result.push_back(std::wstring(fullPath));
        }
    }

    // Remove duplicates while preserving order
    std::vector<std::wstring> unique;
    for (const auto& p : result)
    {
        bool dup = false;
        for (const auto& u : unique)
            if (_wcsicmp(u.c_str(), p.c_str()) == 0) { dup = true; break; }
        if (!dup) unique.push_back(p);
    }

    return unique;
}

// ---------------------------------------------------------------------------
// ParseCommandLine
//
// Manual tokeniser that respects double-quoted arguments.
// argv[0] (the executable name) is skipped.
// ---------------------------------------------------------------------------
std::vector<std::wstring> ParseCommandLine(const wchar_t* cmdLine)
{
    std::vector<std::wstring> tokens;
    if (!cmdLine) return tokens;

    // Skip argv[0]
    const wchar_t* p = cmdLine;
    bool inQuote = false;
    if (*p == L'"') { ++p; while (*p && *p != L'"') ++p; if (*p) ++p; }
    else            { while (*p && *p != L' ' && *p != L'\t') ++p; }
    while (*p == L' ' || *p == L'\t') ++p;

    while (*p)
    {
        std::wstring token;
        inQuote = false;

        while (*p)
        {
            if (*p == L'"')
            {
                inQuote = !inQuote;
                ++p;
                continue;
            }
            if (!inQuote && (*p == L' ' || *p == L'\t'))
                break;
            token += *p++;
        }

        if (!token.empty())
            tokens.push_back(token);

        while (*p == L' ' || *p == L'\t') ++p;
    }

    return tokens;
}

// ---------------------------------------------------------------------------
// PrintVersion
// ---------------------------------------------------------------------------
void PrintVersion()
{
    wprintf(L"windrop %s\n", kVersion);
}

// ---------------------------------------------------------------------------
// PrintUsage
// ---------------------------------------------------------------------------
void PrintUsage(FILE* out)
{
    fwprintf(out,
        L"windrop - CLI Windows Drag-and-Drop Utility\n\n"
        L"Usage:\n"
        L"  windrop <files...>\n"
        L"  windrop -v | --version\n"
        L"  windrop -h | --help\n\n"
        L"Options:\n"
        L"  -v, --version    Show version information\n"
        L"  -h, --help       Show this help message\n\n"
        L"Controls:\n"
        L"  - Hover over target window and press [F8] to drag -> press [F8] or left-click to drop\n"
        L"  - Grab & drag the floating card directly into any window\n"
        L"  - Press [Esc] or right-click on card to dismiss\n\n"
        L"Examples:\n"
        L"  windrop a.txt\n"
        L"  windrop *.png\n"
        L"  windrop file1.pdf file2.png\n"
        L"  windrop \"my report.docx\"\n"
        L"  windrop C:\\Users\\me\\Desktop\\photo.jpg\n");
}

// ---------------------------------------------------------------------------
// PrintError
// ---------------------------------------------------------------------------
void PrintError(const std::wstring& msg)
{
    fwprintf(stderr, L"Error: %s\n", msg.c_str());
}

} // namespace Utils
