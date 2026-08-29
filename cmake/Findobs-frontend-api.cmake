# Locates obs-frontend-api from a local obs-studio build tree.
# Reuses LIBOBS_DIR (see FindLibObs.cmake) as the search root by default.

# obs-studio moved this from UI/obs-frontend-api to frontend/api at some
# point after this module was first written -- the old path is kept as a
# fallback hint for anyone still pointing LIBOBS_DIR at an older checkout,
# but frontend/api is current as of the release this project builds
# against (see .github/workflows/build.yml's pinned obs-studio tag).
if(LIBOBS_DIR)
    set(_ofa_hint_include "${LIBOBS_DIR}/frontend/api" "${LIBOBS_DIR}/UI/obs-frontend-api")
    set(_ofa_hint_lib
        "${LIBOBS_DIR}/build/frontend/api" "${LIBOBS_DIR}/build64/frontend/api"
        "${LIBOBS_DIR}/build/UI/obs-frontend-api" "${LIBOBS_DIR}/build64/UI/obs-frontend-api"
    )
endif()

find_path(OBS_FRONTEND_API_INCLUDE_DIR
    NAMES obs-frontend-api.h
    HINTS ${_ofa_hint_include}
)

# obs-studio's frontend/api/CMakeLists.txt sets PREFIX "" on this target for
# Windows and macOS specifically (Linux keeps CMake's normal "lib" prefix,
# producing libobs-frontend-api.so) -- but find_library still generates its
# search candidates WITH the default "lib" prefix regardless of how the
# actual target was configured, so on macOS it looks for
# "libobs-frontend-api.dylib" and never matches the real, unprefixed
# "obs-frontend-api.dylib" that's actually on disk. Temporarily allowing an
# empty prefix as a candidate (Apple-only; Linux's prefix is unaffected and
# still correct) fixes that mismatch without needing to know obs-studio's
# exact on-disk filename ourselves.
if(APPLE)
    set(_ofa_saved_prefixes ${CMAKE_FIND_LIBRARY_PREFIXES})
    list(APPEND CMAKE_FIND_LIBRARY_PREFIXES "")
endif()

find_library(OBS_FRONTEND_API_LIB
    NAMES obs-frontend-api
    HINTS ${_ofa_hint_lib}
    PATH_SUFFIXES RelWithDebInfo Release Debug
)

if(APPLE)
    set(CMAKE_FIND_LIBRARY_PREFIXES ${_ofa_saved_prefixes})
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(obs-frontend-api DEFAULT_MSG OBS_FRONTEND_API_LIB OBS_FRONTEND_API_INCLUDE_DIR)

if(obs-frontend-api_FOUND AND NOT TARGET OBS::obs-frontend-api)
    add_library(OBS::obs-frontend-api UNKNOWN IMPORTED)
    set_target_properties(OBS::obs-frontend-api PROPERTIES
        IMPORTED_LOCATION "${OBS_FRONTEND_API_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${OBS_FRONTEND_API_INCLUDE_DIR}"
    )
endif()

mark_as_advanced(OBS_FRONTEND_API_INCLUDE_DIR OBS_FRONTEND_API_LIB)
