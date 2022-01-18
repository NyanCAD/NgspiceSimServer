project("NgspiceSimulationServer" CXX)
cmake_minimum_required(VERSION 3.1)
set(CMAKE_CXX_STANDARD 17 CACHE STRING "C++ standard to be used")

find_package(CapnProto CONFIG REQUIRED)
find_library(LIBNGSPICE ngspice REQUIRED)

capnp_generate_cpp(SimulatorSources SimulatorHeaders api/Simulator.capnp)
add_executable(NgspiceSimServer api/main.cpp ${SimulatorSources})
target_link_libraries(NgspiceSimServer PRIVATE CapnProto::capnp-rpc ${LIBNGSPICE})
target_include_directories(NgspiceSimServer PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_PREFIX_PATH}/include)
install(TARGETS NgspiceSimServer)
