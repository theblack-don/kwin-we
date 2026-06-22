# - Try to find KGlobalAccelD (the kglobalacceld daemon library)
#
# kglobalacceld is a separate KDE Frameworks 6 repo (split from
# kglobalaccel) that provides the in-process global shortcuts daemon.
# Its CMake config file is named KF6GlobalAccelDConfig.cmake, but the
# imported target consumers (and this fork) historically call it
# KGlobalAccelD / K::KGlobalAccelD.
#
# This module tries several strategies to locate the library so the
# build still works on distros that ship it under different names
# (Fedora kf6-kglobalacceld-devel, Arch kglobalacceld, etc.).
#
# Once done this will define
#
#  KGlobalAccelD_FOUND         - True if the library was located
#  KGlobalAccelD_INCLUDE_DIRS  - Include directories containing <kglobalacceld.h>
#  KGlobalAccelD_LIBRARIES     - Libraries to link against
#  KGlobalAccelD_VERSION       - Version string, when known
#  K::KGlobalAccelD            - Imported target (use this for target_link_libraries)
#
# SPDX-License-Identifier: BSD-3-Clause

include(FindPackageHandleStandardArgs)
include(FindPkgConfig)

# Internal: track whether we have already created the imported target
set(_KGlobalAccelD_TARGET_CREATED FALSE)

# -----------------------------------------------------------------------
# Helper: create the K::KGlobalAccelD imported target from a located library
# -----------------------------------------------------------------------
function(_kglobalacceld_create_target _lib _include_dir)
    if(_KGlobalAccelD_TARGET_CREATED)
        return()
    endif()
    if(NOT TARGET K::KGlobalAccelD)
        add_library(K::KGlobalAccelD UNKNOWN IMPORTED)
        set_target_properties(K::KGlobalAccelD PROPERTIES
            IMPORTED_LOCATION "${_lib}"
            INTERFACE_INCLUDE_DIRECTORIES "${_include_dir}"
        )
    endif()
    set(_KGlobalAccelD_TARGET_CREATED TRUE
        CACHE INTERNAL "Whether K::KGlobalAccelD imported target has been created" FORCE)
endfunction()

# -----------------------------------------------------------------------
# Strategy 1: upstream CMake config.
# The config file is named either KF6GlobalAccelDConfig.cmake (KF6
# standard naming) or KGlobalAccelDConfig.cmake (legacy naming used
# by the kglobalacceld repo's own CMakeLists.txt, which is what a
# local source build produces). Try the KF6 name first, then the
# legacy name.
# -----------------------------------------------------------------------
find_package(KF6GlobalAccelD QUIET CONFIG)

if(NOT KF6GlobalAccelD_FOUND)
    find_package(KGlobalAccelD QUIET CONFIG)
    if(KGlobalAccelD_FOUND)
        # Adapt the legacy variables to the KF6 names so the rest of
        # this module can use them uniformly.
        set(KF6GlobalAccelD_FOUND TRUE)
        if(NOT KF6GlobalAccelD_VERSION AND KGlobalAccelD_VERSION)
            set(KF6GlobalAccelD_VERSION "${KGlobalAccelD_VERSION}")
        endif()
    endif()
endif()

if(KF6GlobalAccelD_FOUND)
    # The KF6 config file creates a KF6::GlobalAccelD target; the
    # legacy config file creates a KGlobalAccelD::KGlobalAccelD target.
    # Expose it under the K::KGlobalAccelD name the rest of this
    # codebase uses.
    if(NOT TARGET K::KGlobalAccelD)
        if(TARGET KF6::GlobalAccelD)
            add_library(K::KGlobalAccelD INTERFACE IMPORTED)
            target_link_libraries(K::KGlobalAccelD INTERFACE KF6::GlobalAccelD)
        elseif(TARGET KGlobalAccelD::KGlobalAccelD)
            add_library(K::KGlobalAccelD INTERFACE IMPORTED)
            target_link_libraries(K::KGlobalAccelD INTERFACE KGlobalAccelD::KGlobalAccelD)
        endif()
    endif()
    set(KGlobalAccelD_FOUND TRUE)
    set(KGlobalAccelD_VERSION "${KF6GlobalAccelD_VERSION}")
    find_package_handle_standard_args(KGlobalAccelD
        REQUIRED_VARS KGlobalAccelD_FOUND
        VERSION_VAR   KGlobalAccelD_VERSION
    )
    return()
endif()

# -----------------------------------------------------------------------
# Strategy 2: pkg-config. Tries the KF6 .pc name first, then the
# unsuffixed legacy name. Used by Arch, openSUSE Tumbleweed, and
# Fedora variants that don't ship a CMake config.
# -----------------------------------------------------------------------
set(_pc_names "KF6GlobalAccelD" "kglobalacceld")
set(_pc_found FALSE)

foreach(_pc IN LISTS _pc_names)
    pkg_check_modules(PC_KGLOBALACCELD QUIET "${_pc}")
    if(PC_KGLOBALACCELD_FOUND)
        set(_pc_found TRUE)
        break()
    endif()
endforeach()

if(_pc_found)
    # Determine the actual library filename
    find_library(KGlobalAccelD_LIB
        NAMES KF6GlobalAccelD kglobalacceld
        HINTS ${PC_KGLOBALACCELD_LIBRARY_DIRS}
        REQUIRED
    )
    find_path(KGlobalAccelD_INCLUDE_DIR
        NAMES kglobalacceld.h
        HINTS ${PC_KGLOBALACCELD_INCLUDE_DIRS}
    )

    set(KGlobalAccelD_LIBRARIES    "${KGlobalAccelD_LIB}")
    set(KGlobalAccelD_INCLUDE_DIRS "${KGlobalAccelD_INCLUDE_DIR}")
    set(KGlobalAccelD_VERSION      "${PC_KGLOBALACCELD_VERSION}")

    _kglobalacceld_create_target("${KGlobalAccelD_LIB}" "${KGlobalAccelD_INCLUDE_DIR}")

    find_package_handle_standard_args(KGlobalAccelD
        REQUIRED_VARS KGlobalAccelD_LIB KGlobalAccelD_INCLUDE_DIR
        VERSION_VAR   KGlobalAccelD_VERSION
    )
    return()
endif()

# -----------------------------------------------------------------------
# Strategy 3: manual fallback. Last resort for distros that ship neither
# a CMake config nor a .pc file (e.g. a hand-installed /usr/local tree,
# or a local source-tree install whose CMake config lives outside the
# default search path).
# -----------------------------------------------------------------------
# Gather candidate search roots: CMAKE_PREFIX_PATH entries, plus the
# well-known user-local prefix ~/.local. Distros typically install the
# cmake config under <prefix>/lib(64)/cmake/<PackageName>/, and the
# library under <prefix>/lib(64)/, so we derive the library and header
# search roots from CMAKE_PREFIX_PATH.
set(_KGlobalAccelD_SEARCH_ROOTS)
foreach(_root IN LISTS CMAKE_PREFIX_PATH CMAKE_FRAMEWORK_PATH CMAKE_APPBUNDLE_PATH)
    if(IS_ABSOLUTE "${_root}" AND EXISTS "${_root}")
        list(APPEND _KGlobalAccelD_SEARCH_ROOTS "${_root}")
    endif()
endforeach()
if(DEFINED ENV{HOME} AND EXISTS "$ENV{HOME}/.local")
    list(APPEND _KGlobalAccelD_SEARCH_ROOTS "$ENV{HOME}/.local")
endif()

find_library(KGlobalAccelD_LIB
    NAMES KGlobalAccelD KF6GlobalAccelD kglobalacceld
    HINTS ${_KGlobalAccelD_SEARCH_ROOTS}
    PATH_SUFFIXES lib64 lib
)
find_path(KGlobalAccelD_INCLUDE_DIR
    NAMES kglobalacceld.h
    HINTS ${_KGlobalAccelD_SEARCH_ROOTS}
    PATH_SUFFIXES include KGlobalAccelD KF6GlobalAccelD
)

if(KGlobalAccelD_LIB AND KGlobalAccelD_INCLUDE_DIR)
    set(KGlobalAccelD_LIBRARIES    "${KGlobalAccelD_LIB}")
    set(KGlobalAccelD_INCLUDE_DIRS "${KGlobalAccelD_INCLUDE_DIR}")
    _kglobalacceld_create_target("${KGlobalAccelD_LIB}" "${KGlobalAccelD_INCLUDE_DIR}")
endif()

find_package_handle_standard_args(KGlobalAccelD
    REQUIRED_VARS KGlobalAccelD_LIB KGlobalAccelD_INCLUDE_DIR
)

if(KGlobalAccelD_FOUND)
    mark_as_advanced(KGlobalAccelD_LIB KGlobalAccelD_INCLUDE_DIR)
endif()
