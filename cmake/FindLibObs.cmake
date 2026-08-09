# Locates libobs headers/lib from a local obs-studio source+build tree.
#
# Usage:
#   cmake -B build -DLIBOBS_INCLUDE_DIR="C:/obs-studio/libobs" \
#                   -DLIBOBS_LIB="C:/obs-studio/build/libobs/RelWithDebInfo/obs.lib"
# or set LIBOBS_DIR to the root of an obs-studio checkout and this will
# search its conventional subfolders.

if(LIBOBS_DIR)
    set(_libobs_hint_include "${LIBOBS_DIR}/libobs")
    set(_libobs_hint_lib "${LIBOBS_DIR}/build/libobs" "${LIBOBS_DIR}/build64/libobs")
endif()

find_path(LIBOBS_INCLUDE_DIR
    NAMES obs.h
    HINTS ${_libobs_hint_include}
    PATH_SUFFIXES libobs
)

find_library(LIBOBS_LIB
    NAMES obs libobs
    HINTS ${_libobs_hint_lib}
    PATH_SUFFIXES RelWithDebInfo Release Debug
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibObs DEFAULT_MSG LIBOBS_LIB LIBOBS_INCLUDE_DIR)

if(LibObs_FOUND AND NOT TARGET OBS::libobs)
    add_library(OBS::libobs UNKNOWN IMPORTED)
    set_target_properties(OBS::libobs PROPERTIES
        IMPORTED_LOCATION "${LIBOBS_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${LIBOBS_INCLUDE_DIR}"
    )
endif()

mark_as_advanced(LIBOBS_INCLUDE_DIR LIBOBS_LIB)
