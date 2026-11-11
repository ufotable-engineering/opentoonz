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

# Attached to each release so clients can compare without downloading the zip
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/inhouse_version.txt "${INHOUSE_VERSION}")

target_sources(OpenToonz PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/inhouseupdate.h
    ${CMAKE_CURRENT_LIST_DIR}/inhouseupdate.cpp
    ${CMAKE_CURRENT_LIST_DIR}/inhouseversion.h
    ${CMAKE_CURRENT_LIST_DIR}/inhouseversion.cpp
)
