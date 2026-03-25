# PruneStaleTransportPlugins.cmake
#
# Removes stale SmartDashboardTransport_*.dll files from the build output
# directory that do not belong to any currently-configured plugin target.
#
# This prevents deleted or renamed plugins from lingering in the output
# directory and showing up in the Connection menu at runtime.
#
# Required variables:
#   DEPLOY_DIR              - Absolute path to the SmartDashboardApp output directory
#   VALID_TRANSPORT_DLLS    - Semicolon-separated list of expected DLL filenames
#                             (e.g. "SmartDashboardTransport_NT4.dll;SmartDashboardTransport_NativeLink.dll")

if(NOT DEFINED DEPLOY_DIR)
    message(FATAL_ERROR "DEPLOY_DIR must be set")
endif()

if(NOT DEFINED VALID_TRANSPORT_DLLS)
    set(VALID_TRANSPORT_DLLS "")
endif()

file(GLOB _found_dlls "${DEPLOY_DIR}/SmartDashboardTransport_*.dll")

foreach(_dll IN LISTS _found_dlls)
    get_filename_component(_name "${_dll}" NAME)
    list(FIND VALID_TRANSPORT_DLLS "${_name}" _idx)
    if(_idx EQUAL -1)
        message(STATUS "PruneStaleTransportPlugins: removing stale plugin: ${_name}")
        file(REMOVE "${_dll}")
    endif()
endforeach()
