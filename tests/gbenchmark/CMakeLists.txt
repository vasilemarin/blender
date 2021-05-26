# Otherwise we get warnings here that we cant fix in external projects
remove_strict_flags()

if(WITH_BENCHMARK_TESTS AND NOT (WITH_BUILDINFO))
    set(INC
    .
    ../../extern/gbenchmark/include/
    )

    set(INC_SYS

    )

    set(SRC
        gbenchmark_test.cc
    )

    set(LIB
        extern_gbenchmark
    )

    # TODO: check on none linux platforms.
    get_property(_test_libs GLOBAL PROPERTY BLENDER_BENCHMARK_LIBS)
    list(APPEND BENCHMARK_LIBS "-Wl,--whole-archive" ${_test_libs} "-Wl,--no-whole-archive")

    blender_add_test_executable(blender_benchmark "${SRC}" "${INC}" "${INC_SYS}" "${LIB};${BENCHMARK_LIBS}")
endif()
