include(ExternalProject)
include(FetchContent)

if (NOT BUILD_WASM) 
	include(CTest)
endif()

FetchContent_Declare(
	DTL
	GIT_REPOSITORY https://github.com/joeedh/dtl.git
)
FetchContent_MakeAvailable(DTL dtl)

macro(test file extra_lib)
set(target "${file}_out")
add_executable(${target} "${file}")
add_dependencies(${target} dtl)
target_link_libraries(${target} PUBLIC dtl util ${extra_lib} math platform)

#target_include_directories(${target} PUBLIC myers-diff)
#target_include_directories(${target} PUBLIC dtl)

target_include_directories(${target} PUBLIC util)
target_include_directories(${target} PUBLIC math)
target_include_directories(${target} PUBLIC mesh)
target_include_directories(${target} PUBLIC platform)
target_include_directories(${target} PUBLIC binding_generators)
target_include_directories(${target} PUBLIC binding)
target_compile_definitions(${target} PRIVATE LITESTL_TEST_SCALE_DEFAULT=${LITESTL_TEST_SCALE})
# Test inputs must not depend on FMA contraction (gcc defaults to -ffp-contract=fast).
if (CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  target_compile_options(${target} PRIVATE /fp:precise)
else()
  target_compile_options(${target} PRIVATE -ffp-contract=off)
endif()
add_test(NAME ${target} COMMAND ${target})
set_tests_properties(${target} PROPERTIES ENVIRONMENT
  "LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/lsan.supp")
endmacro()

# Heavy randomized suites: labelled "long" so consumers can skip them
# (ctest -LE long); case counts scale with LITESTL_TEST_SCALE.
macro(test_long file extra_lib)
test(${file} "${extra_lib}")
set_tests_properties("${file}_out" PROPERTIES LABELS long)
endmacro()

test(test_boolvector.cc "")
test(test_function.cc "")
test(test_map.cc "")
test(test_math_vec.cc "")
test(test_matrix.cc "")
test(test_pool.cc "")
test(test_set.cc "")
test(test_task.cc "")
test(test_vector.cc "")
test(test_shared_ptr.cc "")
test(test_binding_method.cc "binding")
test(test_binding_constructor.cc "binding")
test(test_binding_system.cc "binding")
test(test_string.cc "")
test(test_binfile.cc "io")
test(test_print_stack_trace.cc "platform")
test(test_value_or_errors_union.cc "")
test(test_value_or_errors_constexpr.cc "")
test_long(test_wideint.cc "")
test(test_hash128.cc "")
test(test_rng.cc "")
test_long(test_predicates.cc predicates_stats)
test(test_determinism_digest.cc predicates)
test(test_predicates_sos.cc predicates)
test(test_rc_buffer.cc "")
test(test_bytes.cc "")
test(test_alloc.cc "")
test(test_sort.cc "")
test(test_attr_column.cc attr)
test_long(test_attr_model.cc attr)
test(test_attr_group.cc attr)
test(test_attr_schema.cc attr)
test(test_attr_pool.cc attr)
test_long(test_attr_pool_model.cc attr)
test(test_attr_pool_threads.cc attr)
test(test_attr_pool_digest.cc attr)
# Compile-fail tests: each snippet must be rejected by the compiler with the
# expected diagnostic (attr schemas hoist attribute shape errors to compile
# time). The control snippet must compile, proving the include setup.
if (NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  set(CF_FLAGS -std=c++20 -fsyntax-only
    -I${CMAKE_CURRENT_SOURCE_DIR}/.. -I${CMAKE_CURRENT_SOURCE_DIR}/../..)
  add_test(NAME compile_ok_schema_control
    COMMAND ${CMAKE_CXX_COMPILER} ${CF_FLAGS} ${CMAKE_CURRENT_SOURCE_DIR}/compile_fail/schema_control.cc)
  macro(compile_fail name regex)
    add_test(NAME compile_fail_${name}
      COMMAND ${CMAKE_CXX_COMPILER} ${CF_FLAGS} ${CMAKE_CURRENT_SOURCE_DIR}/compile_fail/${name}.cc)
    set_tests_properties(compile_fail_${name} PROPERTIES PASS_REGULAR_EXPRESSION "${regex}")
  endmacro()
  compile_fail(schema_unknown_name "no attribute named this in the schema")
  compile_fail(schema_duplicate_name "duplicate attribute names")
  compile_fail(schema_def_not_in_schema "attribute definition is not in the schema")
  compile_fail(schema_wrong_column_type "invalid initialization|cannot bind|non-const lvalue reference")
  compile_fail(schema_not_a_column "constraints not satisfied|not satisfied|AttrDefinition")
endif()
test(test_attr_digest.cc attr)
# A7 probe: same workload compiled asking for FP contraction; attr's PUBLIC
# flags and the kernels' pragmas must keep the digest identical.
add_executable(test_attr_digest_fma test_attr_digest.cc)
add_dependencies(test_attr_digest_fma dtl)
target_link_libraries(test_attr_digest_fma PUBLIC dtl util attr math platform)
target_include_directories(test_attr_digest_fma PUBLIC util math platform)
target_compile_options(test_attr_digest_fma PRIVATE -ffp-contract=fast)
add_test(NAME test_attr_digest_fma COMMAND test_attr_digest_fma)

include(../build_files/macros.cmake)
if (BUILD_WASM)
 	set(LIB2
        PRIVATE platform
        PRIVATE util
        PRIVATE math
        PRIVATE binding
        PRIVATE binding_generators
    )
	
	set(MYWASM "getBindingManager\nmain\nClearMessageBuf\nGetMessageBuf")

	add_executable(test_binding_system "test_binding_system.cc")
	add_link_options(-sINITIAL_MEMORY=943718400)
	add_link_options(-sALLOW_MEMORY_GROWTH=1)
	target_link_libraries(test_binding_system PRIVATE ${LIB2})
	get_target_property(wasm_symbols binding INTERFACE_WASM_SYMBOLS)

	build_wasm_post(test_binding_system "test_binding_system.cc" "${LIB2}" "${wasm_symbols}\n${MYWASM}")
	# 1. Get the directory where the target was defined
	get_target_property(TARGET_SRC_DIR test_binding_system SOURCE_DIR)

	file(GENERATE OUTPUT "${TARGET_SRC_DIR}/test_binding_system.path.txt" CONTENT "$<TARGET_FILE:test_binding_system>")
endif()
# Benchmarks: built, not registered with ctest (run manually in Release).
add_executable(bench_attr bench_attr.cc)
target_link_libraries(bench_attr PUBLIC attr util math platform)
target_include_directories(bench_attr PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
add_executable(bench_attr_pool bench_attr_pool.cc)
target_link_libraries(bench_attr_pool PUBLIC attr util math platform)
target_include_directories(bench_attr_pool PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
