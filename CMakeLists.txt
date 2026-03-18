cmake_minimum_required(VERSION 3.24)

project(SmartDashboardWorkspace LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    set(CMAKE_VS_GLOBALS
        "${CMAKE_VS_GLOBALS};VcpkgXUseBuiltInApplocalDeps=true"
    )
endif()

include(CTest)

option(SMARTDASHBOARD_BUILD_PLUGIN_LEGACY_NT "Build legacy NetworkTables transport plugin" ON)

add_subdirectory(SmartDashboard_Interface_direct)
add_subdirectory(ClientInterface_direct)
add_subdirectory(SmartDashboard)

if(SMARTDASHBOARD_BUILD_PLUGIN_LEGACY_NT)
    add_subdirectory(plugins/LegacyNtTransport)
endif()
