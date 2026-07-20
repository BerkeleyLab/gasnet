#=============================================================================
# tests/CMakeLists.txt - Build and run GASNet tests
#=============================================================================

#---------------------------------------------------------------------
# Test programs (GASNet API tests)
#---------------------------------------------------------------------
set(GASNET_TESTS
  testgasnet testlegacy testtools testinternal testenv testhsl
  testsmall testlarge testval testam testmisc testcore1 testcore2
  testcore3 testcore4 testcore5 testslice testbarrier testbarrierconf
  testbarrierlate testlogGP testalign testrand testexit testvis
  testcoll testcollperf testteambarrier testteambcast testreduce
  testteam testnbr testvisperf testqueue testratomic testfaddperf
  testimm testsplit testacc testsegment testtmpair testreadonly
  testalltoall testtoken testprogthr testmpi testcontend testlockcontend
  testthreads testcxx testlegacycxx testtoolscxx
)

set(TEST_INCLUDES
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${PROJECT_SOURCE_DIR}"
  "${PROJECT_BINARY_DIR}"
)

# Choose the first available conduit for test linking
set(TEST_CONDUIT "smp" CACHE STRING "Conduit to use for building tests")
set(TEST_THREAD_MODE "seq" CACHE STRING "Thread mode for tests: seq, par, or parsync")

# Select the correct library based on conduit and thread mode
set(TEST_LIB "gasnet-${TEST_CONDUIT}-${TEST_THREAD_MODE}")

# Only build tests if the requested library exists
if(TARGET ${TEST_LIB})
  foreach(test_name ${GASNET_TESTS})
    # Determine source file extension
    set(test_ext "c")
    if(test_name STREQUAL "testcxx" OR test_name STREQUAL "testlegacycxx")
      set(test_ext "cc")
    endif()
    if(test_name STREQUAL "testtoolscxx")
      set(test_ext "cc")
    endif()

    set(test_src "${CMAKE_CURRENT_SOURCE_DIR}/${test_name}.${test_ext}")

    if(EXISTS "${test_src}")
      add_executable(${test_name} "${test_src}")
      target_include_directories(${test_name} PRIVATE ${TEST_INCLUDES})
      target_link_libraries(${test_name} PRIVATE ${TEST_LIB})

      # Thread library for par/parsync tests
      if(TEST_THREAD_MODE STREQUAL "par" OR TEST_THREAD_MODE STREQUAL "parsync")
        if(GASNET_THREAD_LIBS)
          target_link_libraries(${test_name} PRIVATE ${GASNET_THREAD_LIBS})
        endif()
      endif()

      # Use MPI C++ linker for C++ tests if MPI is involved
      if(test_name STREQUAL "testcxx" OR test_name STREQUAL "testlegacycxx")
        set_source_files_properties("${test_src}" PROPERTIES LANGUAGE CXX)
      endif()

      # Add test to CTest
      add_test(NAME ${test_name}
        COMMAND ${test_name}
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
      )
    endif()
  endforeach()

  # delay.c is a special test utility compiled with -O0
  add_library(testdelay STATIC "${CMAKE_CURRENT_SOURCE_DIR}/delay.c")
  target_include_directories(testdelay PRIVATE
    ${TEST_INCLUDES}
    "${PROJECT_SOURCE_DIR}/other"
  )
  target_compile_definitions(testdelay PRIVATE -DGASNET_SEQ)
  target_compile_options(testdelay PRIVATE "-O0")
  if(TARGET testlogGP)
    target_link_libraries(testlogGP PRIVATE testdelay)
  endif()
  if(TARGET testbarrierlate)
    target_link_libraries(testbarrierlate PRIVATE testdelay)
  endif()

  # GPU tests - conditionally built
  if(GASNET_KIND_CUDA_UVA_ENABLED AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/testcudauva.c")
    add_executable(testcudauva "${CMAKE_CURRENT_SOURCE_DIR}/testcudauva.c")
    target_include_directories(testcudauva PRIVATE ${TEST_INCLUDES})
    target_link_libraries(testcudauva PRIVATE ${TEST_LIB} ${CUDA_LIBRARIES})
  endif()

  if(GASNET_KIND_HIP_ENABLED AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/testhip.c")
    add_executable(testhip "${CMAKE_CURRENT_SOURCE_DIR}/testhip.c")
    target_include_directories(testhip PRIVATE ${TEST_INCLUDES})
    target_link_libraries(testhip PRIVATE ${TEST_LIB})
  endif()

  if(GASNET_KIND_ZE_ENABLED AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/testze.c")
    add_executable(testze "${CMAKE_CURRENT_SOURCE_DIR}/testze.c")
    target_include_directories(testze PRIVATE ${TEST_INCLUDES})
    target_link_libraries(testze PRIVATE ${TEST_LIB})
  endif()
endif()
