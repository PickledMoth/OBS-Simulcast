# Minimal CPack setup so `cmake --build . --target package` produces a
# zip with the plugin laid out the way OBS expects under its plugins dir.

set(CPACK_PACKAGE_NAME "OBS-Simulcast")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "OBS-Simulcast")
set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_FILE_NAME "OBS-Simulcast-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}")

include(CPack)

# Windows .exe installer (NSIS), built via `cmake --build . --target installer`.
# Not wired into CPack's own NSIS generator because we need custom logic
# (auto-detecting the OBS install dir from the registry) that's easier to
# express directly in installer/installer.nsi than through CPack's NSIS
# component model.
if(WIN32)
    # installer/ isn't part of the public source tree (it's kept locally,
    # not committed) -- released builds are published as prebuilt .exe
    # GitHub Releases instead, not built from a clone. Guarded so a clone
    # without that folder just silently skips this target rather than
    # failing with a confusing "file not found" from NSIS.
    if(EXISTS "${CMAKE_SOURCE_DIR}/installer/installer.nsi")
        find_program(MAKENSIS_EXECUTABLE makensis
            HINTS "$ENV{ProgramFiles\(x86\)}/NSIS" "$ENV{ProgramFiles}/NSIS"
        )
        if(MAKENSIS_EXECUTABLE)
            add_custom_target(installer
                COMMAND ${MAKENSIS_EXECUTABLE}
                    "/DPLUGIN_VERSION=${PROJECT_VERSION}"
                    "/DSOURCE_DIR=$<TARGET_FILE_DIR:OBS-Simulcast>"
                    "/DDATA_DIR=${CMAKE_SOURCE_DIR}/data"
                    "${CMAKE_SOURCE_DIR}/installer/installer.nsi"
                WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
                DEPENDS OBS-Simulcast
                COMMENT "Building Windows installer (NSIS)"
                VERBATIM
            )
        else()
            message(STATUS "makensis not found; `installer` target will be unavailable. Install NSIS to enable it.")
        endif()
    endif()
endif()
