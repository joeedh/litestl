export type pointer<T = any> = number
export type int = number
export type float = number
export type bool = number
export type size_t = number
export type cstring = pointer<number>

export interface IWasmBase {
  LSTL_GetBindingInfo(): pointer
  LSTL_FreeBindingInfo(info: pointer): void
  HEAPU8: Uint8Array

  // tagged allocators, tags are to help track down leaks
  memAlloc(tag: cstring, size: number): pointer
  memRelease(ptr: number): void

  // non-tagged
  _rawAlloc(size: number): pointer
  _rawRelease(ptr: pointer): void
}

export interface IBindingInfo {
  Offsets: {
    Base: {
      name: number
      type: number
    }
    Struct: {
      members: number
      methods: number
      constructors: number
      templateParams: number
      structSize: number
    }
    Constructor: {
      ownerType: number
      params: number
    }
    ConstructorParam: {
      name: number
      type: number
    }
    StructMember: {
      name: number
      offset: number
      type: number
    }
    Number: {
      subtype: number
      flags: number
    }
    Array: {
      arrayType: number
      arraySize: number
    }
    Literal: {
      litType: number
      litBind: number
    }
    NumLit: {
      data: number
    }
    BoolLit: {
      data: number
    }
    StrLit: {
      data: number
    }
    Pointer: {
      ptrType: number
    }
    Reference: {
      refType: number
    }
    EnumItem: {
      name: number
      value: number
    }
    Enum: {
      items: number
      baseSize: number
      isBitMask: number
    }
    TemplateParam: {
      name: number
      type: number
    }
    Method: {
      returnType: number
      params: number
      isConst: number
      isStatic: number
    }
    MethodParam: {
      name: number
      type: number
    }
  }
  Sizes: {
    Struct: {
      StructMember: number
      StructBase: number
      TemplateParam: number
      StructMethod: number
      StructConstructor: number
    }
    Types: {
      Boolean: number
      NumLit: number
      BoolLit: number
      StrLit: number
      Pointer: number
      Reference: number
    }
    Constructor: {
      ConstructorParam: number
    }
    Enum: {
      EnumItem: number
      Enum: number
    }
    Method: {
      Method: number
      MethodParam: number
    }
  }
}

export interface INeededWasm extends IWasmBase {
  // convert js string to cstring, might want to use a string pool for this
  cstring(s: string): cstring

  // convert cstring to js string
  jsString(s: cstring): string

  LSTL_PrintAllocBlocks(includePermanent: boolean): void

  LSTL_Binding_GetKeys(bindingManager: pointer): cstring
  LSTL_Binding_FreeKeys(bindingManager: pointer): cstring
  LSTL_Binding_Get(bindingManager: pointer, key: cstring): pointer
  LSTL_Binding_GetFullName(binding: pointer): cstring
  LSTL_Struct_GetMethodCount(structptr: pointer): size_t
  LSTL_Struct_GetMethod(structptr: pointer, i: size_t): pointer
  LSTL_Method_GetParamCount(method: pointer): size_t
  LSTL_Method_GetParam(method: pointer, i: size_t, outName: pointer<cstring>): pointer
  LSTL_Method_GetReturn(method: pointer): pointer
  LSTL_Method_GetName(method: pointer): cstring
  LSTL_Method_IsConst(method: pointer): int
  LSTL_Method_Invoke(method: pointer, self: pointer, args: pointer<pointer>, retbuf: pointer): void
  LSTL_GenerateTypescript(bindingManager: pointer, sizeOut: pointer<int>): pointer
  LSTL_FreeTypescriptString(buf: pointer): void

  LSTL_Struct_GetConstructorCount(structptr: pointer): size_t
  LSTL_Struct_GetConstructor(structptr: pointer, i: size_t): pointer
  LSTL_Constructor_GetName(constructor: pointer): cstring
  LSTL_Constructor_GetOwner(constructor: pointer): pointer
  LSTL_Constructor_GetParamCount(constructor: pointer): size_t
  LSTL_Constructor_GetParam(constructor: pointer, i: size_t, outName: pointer<cstring>): pointer
  LSTL_Constructor_Invoke(constructor: pointer, outBuf: pointer, args: pointer<pointer>): void
  LSTL_GetBindTypeSize(bindType: pointer): int
  LSTL_Destructor_Invoke(structptr: pointer, obj: pointer): void

  // various helper stuff, create with createWasmHelpers() below
  bindingInfo: IBindingInfo

  HEAPU16: Uint16Array
  HEAPU32: Uint32Array
  HEAPU64: BigUint64Array
  HEAP8: Int8Array
  HEAP16: Int16Array
  HEAP32: Int32Array
  HEAP64: BigInt64Array
  HEAPF32: Float64Array
  HEAPF64: Float64Array
  DATAVIEW: DataView
  HEAPPTR: Uint32Array

  INT8SIZE: number
  INT8SHIFT: number
  INT16SIZE: number
  INT16SHIFT: number
  INT32SIZE: number
  INT32SHIFT: number
  INT64SHIFT: number
  PTRSIZE: number
  PTRSHIFT: number
  F32SIZE: number
  F32SHIFT: number
  F64SIZE: number
  F64SHIFT: number
  SIZET_SIZE: number
  SIZET_SHIFT: number
}

export function createWasmViews(wasmBase: IWasmBase) {
  const array = wasmBase.HEAPU8.buffer
  return {
    HEAP8: new Int8Array(array),
    HEAP16: new Int16Array(array),
    HEAP32: new Int32Array(array),
    HEAP64: new BigInt64Array(array),
    HEAPU16: new Uint16Array(array),
    HEAPU32: new Uint32Array(array),
    HEAPU64: new BigUint64Array(array),
    HEAPPTR: new Uint32Array(array),
    HEAPF32: new Float32Array(array),
    HEAPF64: new Float64Array(array),
    ...wasmBase,
  }
}

function unprefixWasm<T extends IWasmBase>(wasmBase: T): T {
  const wasmAny = wasmBase as unknown as any
  for (let k in wasmAny) {
    if (typeof k !== 'string' || k[0] !== '_') {
      continue
    }
    const unprefixed = k.substring(1)
    wasmAny[unprefixed] = wasmAny[k]
  }
  return wasmBase
}

export function createWasmHelpers<T extends IWasmBase>(wasmBase: T) {
  wasmBase = unprefixWasm(wasmBase)

  const wasm = createWasmViews(wasmBase)
  let ptr = wasm.LSTL_GetBindingInfo()

  let data = wasm.HEAP32
  let i = ptr >> 2
  const Offsets = {
    Base: {
      name: data[i++],
      type: data[i++],
    },
    Struct: {
      members: data[i++],
      methods: data[i++],
      constructors: data[i++],
      templateParams: data[i++],
      structSize: data[i++],
    },
    Constructor: {
      ownerType: data[i++],
      params: data[i++],
    },
    ConstructorParam: {
      name: data[i++],
      type: data[i++],
    },
    StructMember: {
      name: data[i++],
      offset: data[i++],
      type: data[i++],
    },
    Number: {
      subtype: data[i++],
      flags: data[i++],
    },
    Array: {
      arrayType: data[i++],
      arraySize: data[i++],
    },
    Literal: {
      litType: data[i++],
      litBind: data[i++],
    },
    NumLit: {
      data: data[i++],
    },
    BoolLit: {
      data: data[i++],
    },
    StrLit: {
      data: data[i++],
    },
    Pointer: {
      ptrType: data[i++],
    },
    Reference: {
      refType: data[i++],
    },
    EnumItem: {
      name: data[i++],
      value: data[i++],
    },
    Enum: {
      items: data[i++],
      baseSize: data[i++],
      isBitMask: data[i++],
    },
    TemplateParam: {
      name: data[i++],
      type: data[i++],
    },
    Method: {
      returnType: data[i++],
      params: data[i++],
      isConst: data[i++],
      isStatic: data[i++],
    },
    MethodParam: {
      name: data[i++],
      type: data[i++],
    },
  }

  const Sizes = {
    Struct: {
      StructMember: data[i++],
      StructBase: data[i++],
      TemplateParam: data[i++],
      StructMethod: data[i++],
      StructConstructor: data[i++],
    },
    Types: {
      Boolean: data[i++],
      NumLit: data[i++],
      BoolLit: data[i++],
      StrLit: data[i++],
      Pointer: data[i++],
      Reference: data[i++],
    },
    Constructor: {
      ConstructorParam: data[i++],
    },
    Enum: {
      EnumItem: data[i++],
      Enum: data[i++],
    },
    Method: {
      Method: data[i++],
      MethodParam: data[i++],
    },
  }

  const bindingInfo: IBindingInfo = {Offsets, Sizes}
  wasm.LSTL_FreeBindingInfo(ptr)

  const strPool = new Map<string, pointer>()

  return {
    ...wasm,
    cstring(s: string) {
      let ptr = strPool.get(s)
      if (ptr !== undefined) {
        return ptr
      }

      ptr = wasm._rawAlloc(s.length + 1)
      const data = wasm.HEAPU8
      for (let i = 0; i < s.length; i++) {
        data[ptr + i] = s.charCodeAt(i)
      }
      data[ptr + s.length] = 0

      strPool.set(s, ptr)
      return ptr
    },
    jsString(ptr: pointer) {
      const data = wasm.HEAPU8
      let s = ''
      while (data[ptr]) {
        s += String.fromCharCode(data[ptr])
        ptr++
      }
      return s
    },
    HEAPPTR: wasm.HEAPU32,
    INT8SIZE: 1,
    INT8SHIFT: 0,
    INT16SIZE: 2,
    INT16SHIFT: 1,
    INT32SIZE: 4,
    INT32SHIFT: 2,
    INT64SIZE: 8,
    INT64SHIFT: 3,
    PTRSIZE: 4,
    PTRSHIFT: 2,
    F32SIZE: 4,
    F32SHIFT: 2,
    F64SIZE: 8,
    F64SHIFT: 3,
    SIZET_SIZE: 4,
    SIZET_SHIFT: 2,
    bindingInfo,
  } as unknown as T & INeededWasm
}
