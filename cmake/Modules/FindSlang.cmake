# cmake-format: off
# SPDX-FileCopyrightText: Copyright (c) 2026, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
# cmake-format: on

# FindSlang
# ---------
#
# Find the Slang shader compiler library, as installed into the deps prefix by the
# Vulkan SDK build (install_vulkan in scripts/common-functions.sh).
#
# Slang ships its own CMake package (lib/cmake/slang), but its exported targets carry
# the absolute include path of the machine the deps were built on rather than one
# relative to the install, so they cannot be used from a relocated prefix. Hence this.
#
# Result Variables
# ^^^^^^^^^^^^^^^^
#
# This module defines the following variables:
#
#   Slang_INCLUDE_DIR - the directory holding slang.h
#   Slang_LIBRARIES - libraries to link against
#   Slang_STATIC - true if Slang is a static library, which needs SLANG_STATIC defined
#   Slang_FOUND - true if Slang has been found and can be used
#
# The deps build installs Slang as one self-contained static archive,
# libslang-compiler.a (see build_slang in ThirdParty/vulkan/vulkansdk-1.4.363.0),
# which is preferred. A shared install, libslang.so, is still accepted. Either way the
# Slang backend loads no other Slang library at run time.

set(LibPaths
    /usr/lib
    /usr/local/lib)

find_library(Slang_LIBRARY
             NAMES
              libslang-compiler.a
              slang
             HINTS
              ENV LD_LIBRARY_PATH
             PATHS ${LibPaths})

find_path(Slang_INCLUDE_DIR
          NAMES slang.h
          HINTS ${Slang_LIBRARY}/../../include/slang
          PATH_SUFFIXES slang
          PATHS
            /usr/include
            /usr/local/include)

set(Slang_LIBRARIES ${Slang_LIBRARY})
set(Slang_STATIC FALSE)
if(Slang_LIBRARY MATCHES "\\.a$")
  set(Slang_STATIC TRUE)
  find_package(Threads REQUIRED)
  list(APPEND Slang_LIBRARIES Threads::Threads ${CMAKE_DL_LIBS})
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Slang REQUIRED_VARS Slang_LIBRARY Slang_INCLUDE_DIR)
