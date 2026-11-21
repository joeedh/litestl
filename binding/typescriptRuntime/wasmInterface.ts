import {NumberSubtype} from './binding'
import {hooksOf} from './foreign'

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
  _rawGetAllocSize(): number

  wasmMemory: WebAssembly.Memory
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
      isInterface: number
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
      isNonNull: number
      isConst: number
    }
    Reference: {
      refType: number
      isConst: number
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
      templateParams: number
      returnPolicy: number
      jsName: number
      isOptional: number
    }
    MethodParam: {
      name: number
      type: number
    }
    Union: {
      structs: number
      disPropName: number
      disPropType: number
    }
    UnionPair: {
      name: number
      type: number
      typeValue: number
    }
    ParentTemplateParam: {
      templParamName: number
      parentDepth: number
      concreteType: number
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
    Union: {
      Union: number
      UnionPair: number
      typeValue: number
    }
    ParentTemplateParam: {
      ParentTemplateParam: number
    }
    VectorDefaultStaticSize: number
  }
}

export interface IWasmViews {
  HEAPU16: Uint16Array
  HEAPU32: Uint32Array
  HEAPU64: BigUint64Array
  HEAP8: Int8Array
  HEAP16: Int16Array
  HEAP32: Int32Array
  HEAP64: BigInt64Array
  HEAPF32: Float32Array
  HEAPF64: Float64Array
  DATAVIEW: DataView
  HEAPPTR: Uint32Array
}

export interface INeededWasm extends IWasmBase, IWasmViews {
  // convert js string to cstring, might want to use a string pool for this
  cstring(s: string): cstring

  // convert cstring to js string
  jsString(s: cstring): string

  LSTL_PrintAllocBlocks(includePermanent: boolean): void
  LSTL_GetMemSize(includePermanent: boolean): number
  LSTL_FormatBlocks(includePtermanent: boolean): string
  LSTL_FormatBlock(ptr: pointer): string

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

  LSTL_Union_HasDisPropFunc(unionptr: pointer): boolean
  LSTL_Union_RunDisPropFunc(unionptr: pointer, thisPtr: pointer): number

  /** NULL when the registered functions are consistent, else an error to free with LSTL_FreeFormatBlocks. */
  LSTL_Manager_CheckFunctions(bindingManager: pointer): cstring
  LSTL_String_InitAt(dst: pointer, utf8: pointer, len: int): void
  LSTL_String_DestroyAt(dst: pointer): void
  LSTL_FreeFormatBlocks(ptr: pointer): void

  // various helper stuff, create with createWasmHelpers() below
  bindingInfo: IBindingInfo

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

  getNumHeap(num: NumberSubtype, unsigned: boolean): ArrayLike<number>
  getNumShift(num: NumberSubtype): number
}

export function updateWasmViews(wasmBase: IWasmBase & Partial<IWasmViews>, buffer: ArrayBufferLike) {
  wasmBase.HEAPU8 = new Uint8Array(buffer)
  wasmBase.HEAP8 = new Int8Array(buffer)
  wasmBase.HEAP16 = new Int16Array(buffer)
  wasmBase.HEAP32 = new Int32Array(buffer)
  wasmBase.HEAP64 = new BigInt64Array(buffer)
  wasmBase.HEAPU16 = new Uint16Array(buffer)
  wasmBase.HEAPU32 = new Uint32Array(buffer)
  wasmBase.HEAPU64 = new BigUint64Array(buffer)
  wasmBase.HEAPPTR = new Uint32Array(buffer)
  wasmBase.HEAPF32 = new Float32Array(buffer)
  wasmBase.HEAPF64 = new Float64Array(buffer)
  return wasmBase as IWasmBase & IWasmViews
}

export function createWasmViews(wasmBase: IWasmBase) {
  return updateWasmViews({...wasmBase}, wasmBase.HEAPU8.buffer)
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

/** wasmMod: the exported wasm module (usually the es6 default slot)*/
export function createWasmHelpers<T extends IWasmBase>(wasmBase: T, wasmMod: unknown) {
  /* On the module object itself, before it is copied: EM_JS reads Module.__lstlHooks. */
  hooksOf(wasmBase)
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
      members       : data[i++],
      methods       : data[i++],
      constructors  : data[i++],
      templateParams: data[i++],
      structSize    : data[i++],
      isInterface   : data[i++],
    },
    Constructor: {
      ownerType: data[i++],
      params   : data[i++],
    },
    ConstructorParam: {
      name: data[i++],
      type: data[i++],
    },
    StructMember: {
      name  : data[i++],
      offset: data[i++],
      type  : data[i++],
    },
    Number: {
      subtype: data[i++],
      flags  : data[i++],
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
      ptrType  : data[i++],
      isNonNull: data[i++],
      isConst  : data[i++],
    },
    Reference: {
      refType: data[i++],
      isConst: data[i++],
    },
    EnumItem: {
      name : data[i++],
      value: data[i++],
    },
    Enum: {
      items    : data[i++],
      baseSize : data[i++],
      isBitMask: data[i++],
    },
    TemplateParam: {
      name: data[i++],
      type: data[i++],
    },
    Method: {
      returnType    : data[i++],
      params        : data[i++],
      isConst       : data[i++],
      isStatic      : data[i++],
      templateParams: data[i++],
      returnPolicy  : data[i++],
      jsName        : data[i++],
      isOptional    : data[i++],
    },
    MethodParam: {
      name: data[i++],
      type: data[i++],
    },
    Union: {
      structs    : data[i++],
      disPropName: data[i++],
      disPropType: data[i++],
    },
    UnionPair: {
      name     : data[i++],
      type     : data[i++],
      typeValue: data[i++],
    },
    ParentTemplateParam: {
      templParamName: data[i++],
      parentDepth   : data[i++],
      concreteType  : data[i++],
    },
  }

  const Sizes = {
    Struct: {
      StructMember     : data[i++],
      StructBase       : data[i++],
      TemplateParam    : data[i++],
      StructMethod     : data[i++],
      StructConstructor: data[i++],
    },
    Types: {
      Boolean  : data[i++],
      NumLit   : data[i++],
      BoolLit  : data[i++],
      StrLit   : data[i++],
      Pointer  : data[i++],
      Reference: data[i++],
    },
    Constructor: {
      ConstructorParam: data[i++],
    },
    Enum: {
      EnumItem: data[i++],
      Enum    : data[i++],
    },
    Method: {
      Method     : data[i++],
      MethodParam: data[i++],
    },
    Union: {
      Union    : data[i++],
      UnionPair: data[i++],
      typeValue: data[i++],
    },
    ParentTemplateParam: {
      ParentTemplateParam: data[i++],
    },
    VectorDefaultStaticSize: data[i++],
  }

  const bindingInfo: IBindingInfo = {Offsets, Sizes}
  wasm.LSTL_FreeBindingInfo(ptr)

  const strPool = new Map<string, pointer>()

  const encoder = new TextEncoder()
  const decoder = new TextDecoder()

  // Methods read the views through `result`, which is the object
  // updateWasmViews refreshes when memory grows.
  const result = {
    ...wasm,
    /** A NUL-terminated UTF-8 copy of `s` that lives for the process. */
    cstring(s: string) {
      let ptr = strPool.get(s)
      if (ptr !== undefined) {
        return ptr
      }

      const bytes = encoder.encode(s)
      ptr = result._rawAlloc(bytes.length + 1)
      result.HEAPU8.set(bytes, ptr)
      result.HEAPU8[ptr + bytes.length] = 0

      strPool.set(s, ptr)
      return ptr
    },
    jsString(ptr: pointer) {
      const data = result.HEAPU8
      let end = ptr
      while (data[end]) {
        end++
      }
      // TextDecoder rejects views of shared memory; slice() copies.
      return decoder.decode(data.slice(ptr, end))
    },
    getNumHeap(num: NumberSubtype, unsigned: boolean): ArrayLike<number> | BigUint64Array | BigInt64Array {
      switch (num) {
        case NumberSubtype.Int8:
          return unsigned ? result.HEAPU8 : result.HEAP8
        case NumberSubtype.Int16:
          return unsigned ? result.HEAPU16 : result.HEAP16
        case NumberSubtype.Int32:
          return unsigned ? result.HEAPU32 : result.HEAP32
        case NumberSubtype.Int64:
          return unsigned ? result.HEAPU64 : result.HEAP64
        case NumberSubtype.Float32:
          return result.HEAPF32
        case NumberSubtype.Float64:
          return result.HEAPF64
      }
      throw new Error('unknown number type ' + num)
    },
    getNumShift(num: NumberSubtype) {
      switch (num) {
        case NumberSubtype.Int8:
          return 0
        case NumberSubtype.Int16:
          return 1
        case NumberSubtype.Int32:
          return 2
        case NumberSubtype.Int64:
          return 3
        case NumberSubtype.Float32:
          return 2
        case NumberSubtype.Float64:
          return 3
      }
      throw new Error('unknown number type ' + num)
    },
    HEAPPTR    : wasm.HEAPU32,
    INT8SIZE   : 1,
    INT8SHIFT  : 0,
    INT16SIZE  : 2,
    INT16SHIFT : 1,
    INT32SIZE  : 4,
    INT32SHIFT : 2,
    INT64SIZE  : 8,
    INT64SHIFT : 3,
    PTRSIZE    : 4,
    PTRSHIFT   : 2,
    F32SIZE    : 4,
    F32SHIFT   : 2,
    F64SIZE    : 8,
    F64SHIFT   : 3,
    SIZET_SIZE : 4,
    SIZET_SHIFT: 2,
    bindingInfo,
  } as unknown as T & INeededWasm

  // patch .grow on the wasm memory to update views
  const grow = result.wasmMemory.grow
  result.wasmMemory.grow = function (this: WebAssembly.Memory, ...args: Parameters<typeof grow>) {
    const result2 = grow.apply(result.wasmMemory, args)
    updateWasmViews(result, this.buffer)
    return result2
  }

  // Memory grown from inside the module (malloc) never goes through .grow
  // above. Loaders that list their exports (wasiLoader) get every export
  // wrapped to refresh the views after a call that grew memory.
  const exportNames = (wasmBase as unknown as {__exportNames?: string[]}).__exportNames
  if (exportNames) {
    let byteLength = result.wasmMemory.buffer.byteLength
    const resultAny = result as unknown as Record<string, unknown>
    for (const name of exportNames) {
      for (const key of [name, '_' + name, name.startsWith('_') ? name.slice(1) : '']) {
        const fn = resultAny[key]
        if (!key || typeof fn !== 'function') continue
        resultAny[key] = (...args: unknown[]) => {
          const ret = (fn as (...a: unknown[]) => unknown)(...args)
          const buffer = result.wasmMemory.buffer
          if (buffer.byteLength !== byteLength) {
            byteLength = buffer.byteLength
            updateWasmViews(result, buffer)
          }
          return ret
        }
      }
    }
  }

  interface LSTLMemFuncs {
    _LSTL_PrintAllocBlocks(includePermanent: boolean): void
    _LSTL_GetMemSize(includePermanent: boolean): number
    _LSTL_FormatBlocks(includePtermanent: boolean): cstring
    _LSTL_FormatBlock(ptr: pointer): cstring
    _LSTL_FreeFormatBlocks(ptr: pointer): void
  }

  result.LSTL_FormatBlocks = function (includePermanent: boolean) {
    const memfuncs = wasm as unknown as LSTLMemFuncs

    const result2 = memfuncs._LSTL_FormatBlocks(includePermanent)
    const result3 = result.jsString(result2)
    memfuncs._LSTL_FreeFormatBlocks(result2)
    return result3
  }

  result.LSTL_FormatBlock = function (ptr: pointer) {
    const memfuncs = wasm as unknown as LSTLMemFuncs

    const result2 = memfuncs._LSTL_FormatBlock(ptr)
    const result3 = result.jsString(result2)
    memfuncs._LSTL_FreeFormatBlocks(result2)
    return result3
  }

  /*
  Reflect.ownKeys(result)
    .sort((a, b) => ('' + a).toLowerCase().localeCompare(('' + b).toLowerCase()))
    .forEach((a) => console.log(a))
  //*/
  return result
}

export const createWasmMemory = (pages = 19082, maximumPages = 32768, shared = true) => {
  return new WebAssembly.Memory({initial: pages, maximum: maximumPages, shared})
}
