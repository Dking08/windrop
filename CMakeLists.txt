cmake_minimum_required(VERSION 3.16)

project(windrop
    VERSION     2.1.0
    DESCRIPTION "CLI-initiated Windows drag-and-drop utility"
    LANGUAGES   CXX
)

# ──────────────────────────────────────────────────────────────────────────────
# Platform guard – this utility is Windows-only
# ──────────────────────────────────────────────────────────────────────────────
if(NOT WIN32)
    message(FATAL_ERROR "windrop.exe is a Windows-only utility.")
endif()

# ──────────────────────────────────────────────────────────────────────────────
# C++ standard
# ──────────────────────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD          17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

# ──────────────────────────────────────────────────────────────────────────────
# Sources
# ──────────────────────────────────────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/DropSource.cpp
    src/DropSource.h
    src/DataObject.cpp
    src/DataObject.h
    src/DragImage.cpp
    src/DragImage.h
    src/Utils.cpp
    src/Utils.h
)

# ──────────────────────────────────────────────────────────────────────────────
# Executable
#
# WIN32 subsystem flag makes the linker use WinMain as the entry point.
# We still use wmain (console mode) but specify it explicitly; removing WIN32
# here would give us a console window which is fine for a CLI tool.
# We intentionally do NOT pass WIN32 so that the normal console subsystem is
# used – this means stdout/stderr work correctly inside PowerShell / CMD.
# ──────────────────────────────────────────────────────────────────────────────
add_executable(windrop ${SOURCES})

# ──────────────────────────────────────────────────────────────────────────────
# Preprocessor definitions
# ──────────────────────────────────────────────────────────────────────────────
target_compile_definitions(windrop PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    # Target Windows 10 / Server 2016+ for IDragSourceHelper2 and modern APIs
    _WIN32_WINNT=0x0A00
    WINVER=0x0A00
)

# ──────────────────────────────────────────────────────────────────────────────
# Compiler options
# ──────────────────────────────────────────────────────────────────────────────
if(MSVC)
    target_compile_options(windrop PRIVATE
        /W4          # High warning level
        /WX          # Warnings as errors (strict)
        /permissive- # Conformance mode
        /utf-8       # Source and execution charset
        /MP          # Multi-processor compilation
        /experimental:deterministic # Reproducible object code
    )

    # Enable /LTCG (Link-Time Code Generation) for Release builds
    set_target_properties(windrop PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
    )

    # Deterministic linker flags for reproducible PE binary
    target_link_options(windrop PRIVATE
        /Brepro            # Deterministic PE timestamp and checksum
        /PDBALTPATH:%_PDB% # Avoid embedding absolute path to PDB
        /INCREMENTAL:NO    # Disable incremental linking
    )
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(windrop PRIVATE
        -Wall -Wextra -Wpedantic
    )
endif()

# ──────────────────────────────────────────────────────────────────────────────
# Linker libraries
#
#  ole32.lib    – CoInitializeEx, OleInitialize, DoDragDrop, IDataObject, etc.
#  oleaut32.lib – OLE Automation (pulled in transitively by shlobj but listed
#                 explicitly for clarity)
#  shell32.lib  – SHGetFileInfo, IDragSourceHelper, CLSID_DragDropHelper
#  uuid.lib     – GUID/IID symbol definitions (IID_IDataObject, etc.)
#  gdi32.lib    – CreateDIBSection, CreateCompatibleDC, DrawIconEx, TextOut…
#  user32.lib   – SendInput, CreateWindowEx, DefWindowProc, GetDC, …
# ──────────────────────────────────────────────────────────────────────────────
target_link_libraries(windrop PRIVATE
    ole32
    oleaut32
    shell32
    uuid
    gdi32
    user32
)

# ──────────────────────────────────────────────────────────────────────────────
# MSVC Runtime: statically link CRT so the executable is self-contained
# (no dependency on VCRUNTIME140.dll)
# ──────────────────────────────────────────────────────────────────────────────
if(MSVC)
    set_property(TARGET windrop PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
endif()

# ──────────────────────────────────────────────────────────────────────────────
# Installation (optional)
# ──────────────────────────────────────────────────────────────────────────────
install(TARGETS windrop RUNTIME DESTINATION bin)

# ──────────────────────────────────────────────────────────────────────────────
# Summary
# ──────────────────────────────────────────────────────────────────────────────
message(STATUS "")
message(STATUS "=== windrop.exe build configuration ===")
message(STATUS "  Compiler  : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Standard  : C++${CMAKE_CXX_STANDARD}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "========================================")
message(STATUS "")
