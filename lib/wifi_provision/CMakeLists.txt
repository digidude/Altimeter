# Turns provision.html into a plain C string header at CMake configure
# time (not a build-time custom command) so the HTML source stays a
# normal, unescaped .html file to edit. CMAKE_CONFIGURE_DEPENDS makes
# editing provision.html trigger a reconfigure so the header stays in
# sync. Deliberately not using idf_component_register's EMBED_TXTFILES:
# PlatformIO also compiles this component a second time through its
# own generic (non-CMake) builder for LDF/IDE purposes, which neither
# runs our custom commands nor knows about CMAKE_CURRENT_BINARY_DIR.
# Writing the generated header next to wifi_provision.c instead of
# into a CMake build directory means the plain #include "..." quote
# form finds it via ordinary same-directory lookup under *either*
# build system — see .gitignore for why it isn't committed.
#
# Guarded by CMAKE_BUILD_EARLY_EXPANSION: ESP-IDF sets this while doing
# a lightweight include of every component's CMakeLists.txt just to
# read its REQUIRES — during that pass CMAKE_CURRENT_SOURCE_DIR isn't
# reliably this component's directory yet, so skip straight to the
# real idf_component_register() call below instead of misresolving
# provision.html's path.
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    set(PROVISION_HTML_SRC "${CMAKE_CURRENT_LIST_DIR}/provision.html")
    set(PROVISION_HTML_GEN "${CMAKE_CURRENT_LIST_DIR}/provision_html_generated.h")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROVISION_HTML_SRC}")

    file(READ "${PROVISION_HTML_SRC}" _provision_html_raw)
    string(REPLACE "\\" "\\\\" _provision_html_raw "${_provision_html_raw}")
    string(REPLACE "\"" "\\\"" _provision_html_raw "${_provision_html_raw}")
    string(REPLACE "\r" "" _provision_html_raw "${_provision_html_raw}")
    string(REPLACE "\n" "\\n" _provision_html_raw "${_provision_html_raw}")
    file(WRITE "${PROVISION_HTML_GEN}"
        "// Auto-generated from provision.html at CMake configure time — do not edit directly.\n"
        "static const char PROVISION_HTML[] = \"${_provision_html_raw}\";\n")
endif()

idf_component_register(
    SRCS "wifi_provision.c" "dns_server.c" "factory_reset.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_wifi esp_netif esp_event esp_http_server nvs_flash esp_timer esp_driver_gpio
)
