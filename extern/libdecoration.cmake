include(ExternalProject)
ExternalProject_Add(libdecoration
    GIT_REPOSITORY https://gitlab.gnome.org/christian-rauch/libdecoration.git
    GIT_TAG parent_frame
    PREFIX "${CMAKE_CURRENT_BINARY_DIR}/libdecoration"
    CONFIGURE_COMMAND meson --prefix "${CMAKE_BINARY_DIR}" --libdir "lib" ../libdecoration
    BUILD_COMMAND ninja
    INSTALL_COMMAND ninja install
)
