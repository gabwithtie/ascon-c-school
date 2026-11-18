set(ASCON_RESEARCH_OPT_LABEL "unspecified" CACHE STRING "Benchmark optimization label")
set(ASCON_INDEPENDENT_DIR "" CACHE PATH "Directory containing the independent ASCON C sources")

if(ASCON_INDEPENDENT_DIR)
  foreach(source ascon128.c internal-ascon.c aead-common.c ascon128.h internal-ascon.h internal-util.h aead-common.h)
    if(NOT EXISTS "${ASCON_INDEPENDENT_DIR}/${source}")
      message(FATAL_ERROR "Independent implementation is missing ${source} in ${ASCON_INDEPENDENT_DIR}")
    endif()
  endforeach()
  add_library(ascon_independent STATIC
    "${ASCON_INDEPENDENT_DIR}/ascon128.c"
    "${ASCON_INDEPENDENT_DIR}/internal-ascon.c"
    "${ASCON_INDEPENDENT_DIR}/aead-common.c")
  target_include_directories(ascon_independent PUBLIC "${ASCON_INDEPENDENT_DIR}")
endif()

foreach(algorithm ascon128 ascon128a)
  foreach(implementation ref opt64)
    set(ascon_target crypto_aead_${algorithm}v12_${implementation})
    if(NOT TARGET ${ascon_target})
      message(FATAL_ERROR "Research requires target ${ascon_target}; check ALG_LIST and IMPL_LIST.")
    endif()

    set(test_target research_check_${algorithm}_${implementation})
    add_executable(${test_target} correctness.c)
    target_link_libraries(${test_target} PRIVATE ${ascon_target})
    if(ASCON_INDEPENDENT_DIR)
      target_compile_definitions(${test_target} PRIVATE ASCON_INDEPENDENT)
      if(algorithm STREQUAL ascon128a)
        target_compile_definitions(${test_target} PRIVATE ASCON_INDEPENDENT_128A)
      endif()
      target_link_libraries(${test_target} PRIVATE ascon_independent)
    endif()
    add_test(NAME ${test_target} COMMAND ${test_target})

    if(WIN32)
      set(benchmark_target research_bench_${algorithm}_${implementation})
      add_executable(${benchmark_target} benchmark.c)
      target_compile_definitions(${benchmark_target} PRIVATE
        ASCON_ALGORITHM="${algorithm}"
        ASCON_IMPLEMENTATION="${implementation}"
        ASCON_OPTIMIZATION="${ASCON_RESEARCH_OPT_LABEL}")
      target_link_libraries(${benchmark_target} PRIVATE ${ascon_target})
      add_test(NAME ${benchmark_target}_smoke COMMAND ${benchmark_target} --smoke)
    endif()
  endforeach()
endforeach()
