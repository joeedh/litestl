include(ExternalProject)
include(FetchContent)

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
target_include_directories(${target} PUBLIC io)
target_include_directories(${target} PUBLIC mesh)
target_include_directories(${target} PUBLIC platform)
target_include_directories(${target} PUBLIC binding_generators)
target_include_directories(${target} PUBLIC binding)
add_test(NAME ${target} COMMAND ${target})
endmacro()

test(test_boolvector.cc "")
test(test_function.cc "")
test(test_map.cc "")
test(test_math_vec.cc "")
test(test_matrix.cc "")
test(test_set.cc "")
test(test_task.cc "")
test(test_vector.cc "")
test(test_shared_ptr.cc "")
test(test_binding_method.cc "binding")
test(test_binding_constructor.cc "binding")
test(test_string.cc "")
test(test_binfile.cc "io")

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
	target_link_libraries(test_binding_system PRIVATE ${LIB2})
	get_target_property(wasm_symbols binding INTERFACE_WASM_SYMBOLS)

	build_wasm_post(test_binding_system "test_binding_system.cc" "${LIB2}" "${wasm_symbols}\n${MYWASM}")
	# 1. Get the directory where the target was defined
	get_target_property(TARGET_SRC_DIR test_binding_system SOURCE_DIR)

	file(GENERATE OUTPUT "${TARGET_SRC_DIR}/test_binding_system.path.txt" CONTENT "$<TARGET_FILE:test_binding_system>")
endif()