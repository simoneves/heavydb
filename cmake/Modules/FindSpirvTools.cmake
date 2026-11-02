# cmake-format: off
# SPDX-FileCopyrightText: Copyright (c) 2018-2026, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
# cmake-format: on

# FindSpirvTools
# --------
#
# Find SPIRV-Tools, for validating, disassembling and optimizing SPIR-V
#
# Result Variables
# ^^^^^^^^^^^^^^^^
#
# This module defines the following variables:
#
#   SpirvTools_INCLUDE_DIR - include directory for SPIRV-Tools
#   SpirvTools_LIBRARIES - libraries to link against
#   SpirvTools_FOUND - true if SPIRV-Tools has been found and can be used

find_path(SpirvTools_INCLUDE_DIR
          NAMES spirv-tools/libspirv.hpp
          HINTS ${SpirvTools_LIBRARY}/../../include
          PATHS
            /usr/include
            /usr/local/include
            /usr/local/homebrew/include
            /opt/local/include)

set(LibPaths
    /usr/lib
    /usr/local/lib)

find_library(SpirvTools_LIBRARY
             NAMES
              SPIRV-Tools
             HINTS
              ENV LD_LIBRARY_PATH
             PATHS ${LibPaths})

find_library(SpirvTools_Opt_LIBRARY
             NAMES
              SPIRV-Tools-opt
             HINTS
              ENV LD_LIBRARY_PATH
             PATHS ${LibPaths})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SpirvTools REQUIRED_VARS
  SpirvTools_LIBRARY
  SpirvTools_Opt_LIBRARY
  SpirvTools_INCLUDE_DIR
)

if(SpirvTools_FOUND AND NOT TARGET spirv_tools::spirv_tools)
  add_library(spirv_tools::spirv_tools UNKNOWN IMPORTED)
  set_target_properties(spirv_tools::spirv_tools PROPERTIES
    IMPORTED_LOCATION "${SpirvTools_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${SpirvTools_INCLUDE_DIR}"
  )

  add_library(spirv_tools::opt UNKNOWN IMPORTED)
  set_target_properties(spirv_tools::opt PROPERTIES
    IMPORTED_LOCATION "${SpirvTools_Opt_LIBRARY}"
  )
endif()

# The optimizer depends on the core library, so comes first
set(SpirvTools_LIBRARIES
  spirv_tools::opt
  spirv_tools::spirv_tools
)

mark_as_advanced(SpirvTools_INCLUDE_DIR SpirvTools_LIBRARY SpirvTools_Opt_LIBRARY)
