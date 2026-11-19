# N-API runtime for the binding system. Configured only when the node-api
# headers are present (see LITESTL_NODE_API_HEADERS in the top-level CMakeLists).

add_library(binding_napi STATIC napi_runtime.h napi_runtime.cc)
target_link_libraries(binding_napi PUBLIC binding binding_generators util)
target_include_directories(binding_napi PUBLIC "${LITESTL_NODE_API_HEADERS}/include")
# Target N-API 8 (Node 16+): everything the runtime calls is in that version.
target_compile_definitions(binding_napi PUBLIC NAPI_VERSION=8)

if (WIN32)
  # Addons resolve napi_* from the host executable. Windows needs an import
  # library for that; build one from the headers package's .def file.
  # CMAKE_AR is lib.exe under MSVC and llvm-lib under clang-cl; both take /def.
  set(LITESTL_LIB_TOOL "${CMAKE_AR}")
  set(LITESTL_NODE_IMPLIB "${CMAKE_CURRENT_BINARY_DIR}/node.lib" CACHE INTERNAL "")
  add_custom_command(
    OUTPUT "${LITESTL_NODE_IMPLIB}"
    COMMAND "${LITESTL_LIB_TOOL}" /nologo /machine:x64
            "/def:${LITESTL_NODE_API_HEADERS}/def/node_api.def"
            "/out:${LITESTL_NODE_IMPLIB}"
    DEPENDS "${LITESTL_NODE_API_HEADERS}/def/node_api.def"
    COMMENT "Generating node.lib")
  add_custom_target(litestl_node_implib DEPENDS "${LITESTL_NODE_IMPLIB}")
endif()

# Builds a loadable .node addon from `sources`, linked against binding_napi.
function(lt_add_napi_addon target)
  add_library(${target} MODULE ${ARGN})
  target_link_libraries(${target} PRIVATE binding_napi)
  set_target_properties(${target} PROPERTIES PREFIX "" SUFFIX ".node")
  if (WIN32)
    add_dependencies(${target} litestl_node_implib)
    target_link_libraries(${target} PRIVATE "${LITESTL_NODE_IMPLIB}")
  elseif (APPLE)
    target_link_options(${target} PRIVATE "LINKER:-undefined,dynamic_lookup")
  endif()
endfunction()
