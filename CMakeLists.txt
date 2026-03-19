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
option(SMARTDASHBOARD_BUILD_PLUGIN_NATIVE_LINK "Build native generic transport plugin scaffold" OFF)

add_subdirectory(SmartDashboard_Interface_direct)
add_subdirectory(ClientInterface_direct)
add_subdirectory(SmartDashboard)

if(SMARTDASHBOARD_BUILD_PLUGIN_LEGACY_NT)
    add_subdirectory(plugins/LegacyNtTransport)
endif()

if(SMARTDASHBOARD_BUILD_PLUGIN_NATIVE_LINK)
    add_subdirectory(plugins/NativeLinkTransport)
endif()

# Ian: These transport plugins deploy themselves with POST_BUILD copy steps.
# Make the app/test targets depend on the plugin targets so a normal
# `SmartDashboardApp` rebuild also rebuilds and redeploys the enabled plugins,
# instead of silently leaving the runtime plugins directory stale.
if(TARGET SmartDashboardApp AND TARGET LegacyNtTransportPlugin)
    add_dependencies(SmartDashboardApp LegacyNtTransportPlugin)
endif()

if(TARGET SmartDashboardApp AND TARGET NativeLinkTransportPlugin)
    add_dependencies(SmartDashboardApp NativeLinkTransportPlugin)
endif()

if(BUILD_TESTING AND TARGET SmartDashboard_tests AND TARGET LegacyNtTransportPlugin)
    add_dependencies(SmartDashboard_tests LegacyNtTransportPlugin)
endif()

if(BUILD_TESTING AND TARGET SmartDashboard_tests AND TARGET NativeLinkTransportPlugin)
    add_dependencies(SmartDashboard_tests NativeLinkTransportPlugin)
endif()
