set(INHOUSE_VERSION ""
    CACHE STRING "In-house CalVer (YYYY.MM.N) or 'dev'. Empty disables in-house behavior.")
set(INHOUSE_RELEASE_URL "https://github.com/ufotable-engineering/opentoonz/releases"
    CACHE STRING "Releases page whose latest/download/inhouse_version.txt is checked on launch.")

if(NOT INHOUSE_VERSION MATCHES "^([0-9][0-9][0-9][0-9]\\.[0-9][0-9]\\.[0-9]+|dev)?$")
    message(FATAL_ERROR "INHOUSE_VERSION must be YYYY.MM.N, 'dev' or empty: ${INHOUSE_VERSION}")
endif()

configure_file(
    ${CMAKE_CURRENT_LIST_DIR}/inhouseversion_config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/inhouseversion_config.h
    @ONLY
)

target_sources(OpenToonz PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/inhouseversion.h
    ${CMAKE_CURRENT_LIST_DIR}/inhouseversion.cpp
)
