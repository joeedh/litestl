set(LIB
    eigen
    util
)

set(SRC
  PUBLIC binding.h
  PUBLIC binding_base.h
  PUBLIC binding_types.h
  PUBLIC binding_utils.h
  PUBLIC binding_method.h
  PUBLIC binding_constructor.h
  binding.cc
)

lt_add_library(binding "${SRC}" "${LIB}" STATIC)
add_subdirectory(generators)
set(WASMSYM "
  LSTL_Binding_GetKeys
  LSTL_Binding_FreeKeys
  LSTL_Binding_Get
  LSTL_Struct_GetMethodCount
  LSTL_Struct_GetMethod
  LSTL_Method_GetParamCount
  LSTL_Method_GetParam
  LSTL_Method_GetReturn
  LSTL_Method_GetName
  LSTL_Method_IsConst
  LSTL_Method_Invoke
  LSTL_Struct_GetConstructorCount
  LSTL_Struct_GetConstructor
  LSTL_Constructor_GetName
  LSTL_Constructor_GetOwner
  LSTL_Constructor_GetParamCount
  LSTL_Constructor_GetParam
  LSTL_Constructor_Invoke
  LSTL_Destructor_Invoke
  LSTL_GetBindingInfo
  LSTL_FreeBindingInfo
  LSTL_GenerateTypescript
  LSTL_FreeTypescriptString
  LSTL_GetBindTypeSize
  LSTL_PrintAllocBlocks
  LSTL_Binding_GetFullName
  printMemBlocks
  _rawAlloc
  _rawRelease
  memAlloc
  memRelease
  "
)

lt_wasm_add_symbols("${WASMSYM}")
set_target_properties(binding PROPERTIES 
    INTERFACE_WASM_SYMBOLS "${WASMSYM}"
)
