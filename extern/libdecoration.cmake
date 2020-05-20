include(ExternalProject)
ExternalProject_Add(libdecoration
    GIT_REPOSITORY https://gitlab.gnome.org/jadahl/libdecoration.git
    GIT_TAG master
    PREFIX "${CMAKE_CURRENT_BINARY_DIR}/libdecoration"
    CONFIGURE_COMMAND meson --prefix "${CMAKE_BINARY_DIR}" --libdir "lib" ../libdecoration
    BUILD_COMMAND ninja
    INSTALL_COMMAND ninja install
)
