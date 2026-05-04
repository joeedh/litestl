import {NumberSubtype} from './binding'

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
    },
    Reference: {
      refType: data[i++],
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
      returnType: data[i++],
      params    : data[i++],
      isConst   : data[i++],
      isStatic  : data[i++],
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
  }

  const bindingInfo: IBindingInfo = {Offsets, Sizes}
  wasm.LSTL_FreeBindingInfo(ptr)

  const strPool = new Map<string, pointer>()

  const result = {
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
    getNumHeap(num: NumberSubtype, unsigned: boolean): ArrayLike<number> | BigUint64Array | BigInt64Array {
      switch (num) {
        case NumberSubtype.Int8:
          return unsigned ? wasm.HEAPU8 : wasm.HEAP8
        case NumberSubtype.Int16:
          return unsigned ? wasm.HEAPU16 : wasm.HEAP16
        case NumberSubtype.Int32:
          return unsigned ? wasm.HEAPU32 : wasm.HEAP32
        case NumberSubtype.Int64:
          return unsigned ? wasm.HEAPU64 : wasm.HEAP64
        case NumberSubtype.Float32:
          return wasm.HEAPF32
        case NumberSubtype.Float64:
          return wasm.HEAPF64
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

  /*
  Reflect.ownKeys(result)
    .sort((a, b) => ('' + a).toLowerCase().localeCompare(('' + b).toLowerCase()))
    .forEach((a) => console.log(a))
  //*/
  return result
}

export const createWasmMemory = (pages = 6400, maximumPages = 32768, shared = true) => {
  return new WebAssembly.Memory({initial: pages, maximum: maximumPages, shared})
}
