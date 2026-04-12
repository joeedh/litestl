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
