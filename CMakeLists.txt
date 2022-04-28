# SPDX-FileCopyrightText: 2022 Pepijn de Vos
#
# SPDX-License-Identifier: MPL-2.0

project("NgspiceSimulationServer" CXX)
cmake_minimum_required(VERSION 3.1)
set(CMAKE_CXX_STANDARD 17 CACHE STRING "C++ standard to be used")

find_package(Boost REQUIRED COMPONENTS system filesystem)
find_package(CapnProto CONFIG REQUIRED)
find_library(LIBNGSPICE ngspice REQUIRED)

capnp_generate_cpp(SimulatorSources SimulatorHeaders api/Simulator.capnp)
add_executable(NgspiceSimServer api/main.cpp ${SimulatorSources})
target_link_libraries(NgspiceSimServer PRIVATE ${CMAKE_DL_LIBS} CapnProto::capnp-rpc ${LIBNGSPICE} ${Boost_LIBRARIES})
target_include_directories(NgspiceSimServer PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_PREFIX_PATH}/include ${Boost_INCLUDE_DIRS})
install(TARGETS NgspiceSimServer)
