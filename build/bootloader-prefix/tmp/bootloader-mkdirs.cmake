# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/opt/esp-idf/components/bootloader/subproject")
  file(MAKE_DIRECTORY "/opt/esp-idf/components/bootloader/subproject")
endif()
file(MAKE_DIRECTORY
  "/home/shaun/src/ld2450-zb-h2/ld2450_zb_h2/build/bootloader"
  "/home/shaun/src/ld2450-zb-h2/ld2450_zb_h2/build/bootloader-prefix"
  "/home/shaun/src/ld2450-zb-h2/ld2450_zb_h2/build/bootloader-prefix/tmp"
  "/home/shaun/src/ld2450-zb-h2/ld2450_zb_h2/build/bootloader-prefix/src/bootloader-stamp"
  "/home/shaun/src/ld2450-zb-h2/ld2450_zb_h2/build/bootloader-prefix/src"
  "/home/shaun/src/ld2450-zb-h2/ld2450_zb_h2/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/shaun/src/ld2450-zb-h2/ld2450_zb_h2/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/shaun/src/ld2450-zb-h2/ld2450_zb_h2/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
