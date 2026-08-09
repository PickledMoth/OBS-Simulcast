# Locates obs-frontend-api from a local obs-studio build tree.
# Reuses LIBOBS_DIR (see FindLibObs.cmake) as the search root by default.

if(LIBOBS_DIR)
    set(_ofa_hint_include "${LIBOBS_DIR}/UI/obs-frontend-api")
    set(_ofa_hint_lib "${LIBOBS_DIR}/build/UI/obs-frontend-api" "${LIBOBS_DIR}/build64/UI/obs-frontend-api")
endif()

find_path(OBS_FRONTEND_API_INCLUDE_DIR
    NAMES obs-frontend-api.h
    HINTS ${_ofa_hint_include}
)

find_library(OBS_FRONTEND_API_LIB
    NAMES obs-frontend-api
    HINTS ${_ofa_hint_lib}
    PATH_SUFFIXES RelWithDebInfo Release Debug
)

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
