# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ld2450_zb_h2)

# Force-pull ZGP handler stub from libmain.a (link order otherwise drops it)
idf_build_set_property(LINK_OPTIONS "-Wl,-u,zb_zcl_green_power_cluster_handler" APPEND)
