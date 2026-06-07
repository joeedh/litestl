set(SRC
  ./include/eigen5/Eigen
)

lt_add_library(eigen "${SRC}" "" "INTERFACE")

option(WITH_CHOLMOD "Build SuiteSparse CHOLMOD" ON)

if(WITH_CHOLMOD)
  # Default to static-only; caller can override before including this file.
  if(NOT DEFINED BUILD_SHARED_LIBS)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries")
  endif()
  if(NOT DEFINED BUILD_STATIC_LIBS)
    set(BUILD_STATIC_LIBS ON CACHE BOOL "Build static libraries")
  endif()

  # --- OpenBLAS (provides both BLAS and LAPACK for CHOLMOD supernodal) ----------
  # SuiteSparse probes and links BLAS/LAPACK during *its* configure step, so the
  # library must already be built by then. add_subdirectory() only schedules targets
  # for the build phase — too late. Instead we configure + build + install OpenBLAS
  # in its own tree right here, at configure time, before add_subdirectory(SuiteSparse).
  set(OPENBLAS_SRC_DIR     "${CMAKE_CURRENT_LIST_DIR}/../OpenBLAS")
  set(OPENBLAS_BUILD_DIR   "${CMAKE_CURRENT_BINARY_DIR}/openblas-build")
  set(OPENBLAS_INSTALL_DIR "${CMAKE_CURRENT_BINARY_DIR}/openblas-install")

  set(_openblas_cmake_args
    -G "${CMAKE_GENERATOR}"
    "-DCMAKE_INSTALL_PREFIX=${OPENBLAS_INSTALL_DIR}"
    "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
    # No Fortran (portable C reference LAPACK); BLAS kernels stay C/assembly.
    -DNOFORTRAN=ON
    -DCMAKE_CXX_FLAGS="-Wno-unused-variable -Wno-unused-function"
    -DBUILD_WITHOUT_LAPACK=OFF
    # CHOLMOD uses the Fortran BLAS interface, not CBLAS; skip CBLAS + its utest/ctest.
    -DBUILD_WITHOUT_CBLAS=ON
    -DBUILD_TESTING=OFF
    -DBUILD_SHARED_LIBS=OFF
    -DBUILD_STATIC_LIBS=ON)

  # Match the parent toolchain so the static lib is ABI-compatible.
  if(CMAKE_TOOLCHAIN_FILE)
    if(IS_ABSOLUTE "${CMAKE_TOOLCHAIN_FILE}")
      set(_openblas_toolchain "${CMAKE_TOOLCHAIN_FILE}")
    else()
      get_filename_component(_openblas_toolchain "${CMAKE_BINARY_DIR}/${CMAKE_TOOLCHAIN_FILE}" ABSOLUTE)
    endif()
    list(APPEND _openblas_cmake_args "-DCMAKE_TOOLCHAIN_FILE=${_openblas_toolchain}")
  else()
    if(CMAKE_C_COMPILER)
      list(APPEND _openblas_cmake_args "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}")
    endif()
    if(CMAKE_CXX_COMPILER)
      list(APPEND _openblas_cmake_args "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}")
    endif()
  endif()
  if(CMAKE_GENERATOR_PLATFORM)
    list(APPEND _openblas_cmake_args -A "${CMAKE_GENERATOR_PLATFORM}")
  endif()
  if(CMAKE_GENERATOR_TOOLSET)
    list(APPEND _openblas_cmake_args -T "${CMAKE_GENERATOR_TOOLSET}")
  endif()

  message(STATUS "Configuring bundled OpenBLAS -> ${OPENBLAS_BUILD_DIR}")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" ${_openblas_cmake_args}
            -S "${OPENBLAS_SRC_DIR}" -B "${OPENBLAS_BUILD_DIR}"
    RESULT_VARIABLE _openblas_cfg)
  if(_openblas_cfg)
    message(FATAL_ERROR "OpenBLAS configure failed (${_openblas_cfg})")
  endif()

  message(STATUS "Building bundled OpenBLAS (this can take a while on a clean tree)")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${OPENBLAS_BUILD_DIR}"
            --config "${CMAKE_BUILD_TYPE}" --parallel
    RESULT_VARIABLE _openblas_build)
  if(_openblas_build)
    message(FATAL_ERROR "OpenBLAS build failed (${_openblas_build})")
  endif()

  execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${OPENBLAS_BUILD_DIR}" --config "${CMAKE_BUILD_TYPE}"
    RESULT_VARIABLE _openblas_install)
  if(_openblas_install)
    message(FATAL_ERROR "OpenBLAS install failed (${_openblas_install})")
  endif()

  file(GLOB _openblas_libs
    "${OPENBLAS_INSTALL_DIR}/lib/libopenblas*.a"
    "${OPENBLAS_INSTALL_DIR}/lib/openblas*.lib"
    "${OPENBLAS_INSTALL_DIR}/lib/libopenblas*.lib")
  if(NOT _openblas_libs)
    message(FATAL_ERROR "OpenBLAS built but no static library under ${OPENBLAS_INSTALL_DIR}/lib")
  endif()
  list(GET _openblas_libs 0 OPENBLAS_LIBRARY)

  set(BLA_VENDOR OpenBLAS CACHE STRING "BLAS vendor" FORCE)

  # Pre-supply BLAS/LAPACK so SuiteSparseBLAS.cmake / SuiteSparseLAPACK.cmake
  # return early without searching (their lines 65-74 / 23-27).
  set(BLAS_LIBRARIES   "${OPENBLAS_LIBRARY}" CACHE FILEPATH "BLAS library"   FORCE)
  set(LAPACK_LIBRARIES "${OPENBLAS_LIBRARY}" CACHE FILEPATH "LAPACK library" FORCE)
  set(BLAS_FOUND   TRUE CACHE BOOL "" FORCE)
  set(LAPACK_FOUND TRUE CACHE BOOL "" FORCE)
  # SuiteSparseBLAS32/64 and SuiteSparseLAPACK.cmake key off BLA_VENDOR for
  # compile-time definitions (e.g. threading model, openblas_set_num_threads_local).

  # --- SuiteSparse / CHOLMOD ----------------------------------------------------
  # Only build the packages CHOLMOD actually needs.
  set(SUITESPARSE_ENABLE_PROJECTS "suitesparse_config;amd;colamd;camd;ccolamd;cholmod"
      CACHE STRING "SuiteSparse projects to build" FORCE)
  # Keep SuiteSparse noise-free: no demos, no tests.
  set(SUITESPARSE_DEMOS OFF CACHE BOOL "Build SuiteSparse demos" FORCE)

  add_subdirectory(SuiteSparse)
endif()
