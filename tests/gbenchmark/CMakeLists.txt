if(WITH_BENCHMARK_TESTS)
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

    blender_add_test_executable(benchmark "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")
endif()
