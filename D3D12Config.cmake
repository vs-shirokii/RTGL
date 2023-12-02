# ==============================================================================
# LLVM Release License
# ==============================================================================
# University of Illinois/NCSA
# Open Source License
# 
# Copyright (c) 2003-2015 University of Illinois at Urbana-Champaign.
# All rights reserved.
# 
# Developed by:
# 
#     LLVM Team
# 
#     University of Illinois at Urbana-Champaign
# 
#     http://llvm.org
# 
# Permission is hereby granted, free of charge, to any person obtaining a copy of
# this software and associated documentation files (the "Software"), to deal with
# the Software without restriction, including without limitation the rights to
# use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
# of the Software, and to permit persons to whom the Software is furnished to do
# so, subject to the following conditions:
# 
#     * Redistributions of source code must retain the above copyright notice,
#       this list of conditions and the following disclaimers.
# 
#     * Redistributions in binary form must reproduce the above copyright notice,
#       this list of conditions and the following disclaimers in the
#       documentation and/or other materials provided with the distribution.
# 
#     * Neither the names of the LLVM Team, University of Illinois at
#       Urbana-Champaign, nor the names of its contributors may be used to
#       endorse or promote products derived from this Software without specific
#       prior written permission.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
# FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
# CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS WITH THE
# SOFTWARE.
# 
# ==============================================================================
# Copyrights and Licenses for Third Party Software Distributed with LLVM:
# ==============================================================================
# The LLVM software contains code written by third parties.  Such software will
# have its own individual LICENSE.TXT file in the directory in which it appears.
# This file will describe the copyrights, license, and restrictions which apply
# to that code.
# 
# The disclaimer of warranty in the University of Illinois Open Source License
# applies to all code in the LLVM Distribution, and nothing in any of the
# other licenses gives permission to use the names of the LLVM Team or the
# University of Illinois to endorse or promote products derived from this
# Software.

# Find the Win10 SDK path.
if ("$ENV{WIN10_SDK_PATH}$ENV{WIN10_SDK_VERSION}" STREQUAL "" )
  get_filename_component(WIN10_SDK_PATH "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots;KitsRoot10]" ABSOLUTE CACHE)
  if (CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION)
    set (WIN10_SDK_VERSION ${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION})
  else()
    # CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION may not be defined if, for example,
    # the Ninja generator is used instead of Visual Studio. Attempt to retrieve the
    # most recent SDK version from the list of paths under "${WIN10_SDK_PATH}/Include/".
    file(GLOB sdk_dirs RELATIVE "${WIN10_SDK_PATH}/Include/" "${WIN10_SDK_PATH}/Include/10.*")
    if (sdk_dirs)
      list(POP_BACK sdk_dirs WIN10_SDK_VERSION)
    endif()
    unset(sdk_dirs)
  endif()
elseif(TRUE)
  set (WIN10_SDK_PATH $ENV{WIN10_SDK_PATH})
  set (WIN10_SDK_VERSION $ENV{WIN10_SDK_VERSION})
endif ("$ENV{WIN10_SDK_PATH}$ENV{WIN10_SDK_VERSION}" STREQUAL "" )

# WIN10_SDK_PATH will be something like C:\Program Files (x86)\Windows Kits\10
# WIN10_SDK_VERSION will be something like 10.0.14393 or 10.0.14393.0; we need the
# one that matches the directory name.

if (IS_DIRECTORY "${WIN10_SDK_PATH}/Include/${WIN10_SDK_VERSION}.0")
  set(WIN10_SDK_VERSION "${WIN10_SDK_VERSION}.0")
endif (IS_DIRECTORY "${WIN10_SDK_PATH}/Include/${WIN10_SDK_VERSION}.0")


# Find the d3d12 and dxgi include path, it will typically look something like this.
# C:\Program Files (x86)\Windows Kits\10\Include\10.0.10586.0\um\d3d12.h
# C:\Program Files (x86)\Windows Kits\10\Include\10.0.10586.0\shared\dxgi1_4.h
find_path(D3D12_INCLUDE_DIR    # Set variable D3D12_INCLUDE_DIR
          d3d12.h                # Find a path with d3d12.h
          HINTS "${WIN10_SDK_PATH}/Include/${WIN10_SDK_VERSION}/um"
          DOC "path to WIN10 SDK header files"
          HINTS
          )

find_path(DXGI_INCLUDE_DIR    # Set variable DXGI_INCLUDE_DIR
          dxgi1_4.h           # Find a path with dxgi1_4.h
          HINTS "${WIN10_SDK_PATH}/Include/${WIN10_SDK_VERSION}/shared"
          DOC "path to WIN10 SDK header files"
          HINTS
          )
set(D3D12_INCLUDE_DIRS ${D3D12_INCLUDE_DIR} ${DXGI_INCLUDE_DIR})

# List of D3D libraries
set(D3D12_LIBRARIES d3d12.lib dxgi.lib d3dcompiler.lib)

include(FindPackageHandleStandardArgs)
# handle the QUIETLY and REQUIRED arguments and set D3D12_FOUND to TRUE
# if all listed variables are TRUE
find_package_handle_standard_args(D3D12  DEFAULT_MSG
                                  D3D12_INCLUDE_DIRS D3D12_LIBRARIES)

mark_as_advanced(D3D12_INCLUDE_DIRS D3D12_LIBRARIES)
