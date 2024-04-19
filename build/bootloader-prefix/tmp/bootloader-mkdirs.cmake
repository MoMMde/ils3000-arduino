# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/moritz/fun/esp/esp-idf/components/bootloader/subproject"
  "/home/moritz/fun/ils3000-arduino/build/bootloader"
  "/home/moritz/fun/ils3000-arduino/build/bootloader-prefix"
  "/home/moritz/fun/ils3000-arduino/build/bootloader-prefix/tmp"
  "/home/moritz/fun/ils3000-arduino/build/bootloader-prefix/src/bootloader-stamp"
  "/home/moritz/fun/ils3000-arduino/build/bootloader-prefix/src"
  "/home/moritz/fun/ils3000-arduino/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/moritz/fun/ils3000-arduino/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/moritz/fun/ils3000-arduino/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
