# The binding conformance fixture: one set of bound C++ types and functions,
# shared by the native generator test (tests/test_binding_functions.cc), the
# WASM module and the N-API addon.

add_library(binding_fixture STATIC fixture.h fixture.cc)
target_link_libraries(binding_fixture PUBLIC binding util math platform)

if (NOT BUILD_WASM)
  add_executable(binding_fixture_gen_ts gen_ts.cc)
  target_link_libraries(binding_fixture_gen_ts PRIVATE binding_fixture binding_generators)
endif()

if (TARGET binding_napi)
  lt_add_napi_addon(binding_fixture_napi napi_addon.cc)
  target_link_libraries(binding_fixture_napi PRIVATE binding_fixture)
endif()

if (BUILD_WASM AND LITESTL_WASM_CLANG_TOOLCHAIN)
  add_executable(binding_fixture_wasm wasm_entry.cc)
  set_target_properties(binding_fixture_wasm PROPERTIES SUFFIX ".wasm")
  target_link_libraries(binding_fixture_wasm PRIVATE binding_fixture binding_generators)
  get_target_property(_fixture_syms binding INTERFACE_WASM_SYMBOLS)
  string(REGEX REPLACE "[ \t\r\n]+" ";" _fixture_syms "${_fixture_syms}")
  list(APPEND _fixture_syms LSTL_Fixture_GetManager)
  foreach(sym IN LISTS _fixture_syms)
    if (sym)
      target_link_options(binding_fixture_wasm PRIVATE "LINKER:--export=${sym}")
    endif()
  endforeach()
endif()

# The JS conformance suite (js/run.mjs) against the N-API addon, plus the WASM
# module when LITESTL_CONFORMANCE_WASM names one built by the wasm preset.
if (TARGET binding_fixture_napi AND TARGET binding_fixture_gen_ts)
  find_program(LITESTL_NODE node)
  set(LITESTL_CONFORMANCE_WASM "" CACHE FILEPATH
      "binding_fixture_wasm.wasm for the conformance test (optional)")
  if (LITESTL_NODE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/js/node_modules")
    set(_conf_args
        --gen-exe $<TARGET_FILE:binding_fixture_gen_ts>
        --napi $<TARGET_FILE:binding_fixture_napi>
        --out "${CMAKE_CURRENT_BINARY_DIR}/conformance")
    if (LITESTL_CONFORMANCE_WASM)
      list(APPEND _conf_args --wasm "${LITESTL_CONFORMANCE_WASM}")
    endif()
    add_test(NAME binding_conformance
             COMMAND "${LITESTL_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/js/run.mjs" ${_conf_args})
  else()
    message(STATUS "litestl: binding_conformance needs node and `npm ci` in tests/bindings/js")
  endif()
endif()
