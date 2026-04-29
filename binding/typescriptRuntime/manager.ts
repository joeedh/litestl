import {WasmBase} from './wasmBase'
import {INeededWasm, pointer} from './wasmInterface'
import {
  BindingBase,
  Binding,
  getBinding,
  BindingType,
  StructType,
  ConstructorType,
  ArrayType,
  MethodType,
  NumberSubtype,
  NumberFlags,
} from './binding'
import {createBoundType, IBoundWasmConstructor} from './bind'
import {BoundArray, BoundVector} from './boundVector'

export class NotStructError extends Error {}
export class UnknownTypeError extends Error {}
export class UnknownConstructorError extends Error {}

export class BindingManager<
  WASM extends INeededWasm = INeededWasm,
  AllBoundTypes extends {[k: string]: unknown} = {[k: string]: unknown},
> extends WasmBase<WASM> {
  types = new Map<string, BindingBase>()
  boundClasses = new Map<string, unknown>()
  //boundPointers = new Map<string, unknown>()

  constructor(wasm: WASM, ptr: pointer) {
    super(wasm, ptr)
    // do not enumerate this.wasm in console.log
    Object.defineProperty(this, 'wasm', {enumerable: false, configurable: true})
  }

  getBoundClass<S extends StructType>(type: S): IBoundWasmConstructor<S['boundType']> {
    type CLS = IBoundWasmConstructor<S['boundType']>
    let cls = this.boundClasses.get(type.name)
    if (cls) {
      return cls as CLS
    }

    cls = createBoundType<this>(this, this.wasm, type)
    this.boundClasses.set(type.name, cls)
    return cls as CLS
  }

  getBoundArray(arrayTypeName: string, ptr: pointer) {
    const arrayType = this.types.get(arrayTypeName)
    if (arrayType === undefined) {
      throw new UnknownTypeError('unknown type ' + arrayTypeName)
    }
    return new BoundArray(this.wasm, ptr, arrayType as ArrayType, this)
  }

  getBoundVector(vecTypeName: string, ptr: pointer) {
    const vecType = this.types.get(vecTypeName)
    if (vecType === undefined) {
      throw new UnknownTypeError('unknown type ' + vecTypeName)
    }
    return new BoundVector(this.wasm, ptr, vecType as StructType, this)
  }

  getBoundPointer<K extends keyof AllBoundTypes>(typeName: K, ptr: pointer): AllBoundTypes[K] | undefined
  getBoundPointer(typeName: string | Binding, ptr: pointer): number | WasmBase<WASM> | undefined
  getBoundPointer(typeName: string | Binding, ptr: pointer) {
    const binding = typeof typeName === 'string' ? this.get(typeName) : typeName
    if (binding === undefined) {
      throw new UnknownTypeError('unknown type ' + typeName)
    }

    if (binding.type === BindingType.Struct && binding.isVector) {
      return this.getBoundVector(binding.name, ptr)
    }

    const wasm = this.wasm

    switch (binding.type) {
      case BindingType.Array:
        return this.getBoundArray(binding.name, ptr)
      case BindingType.Number: {
        const unsigned = binding.flags & NumberFlags.Unsigned

        switch (binding.subtype) {
          case NumberSubtype.Float32:
            return wasm.HEAPF32[ptr >> wasm.F32SHIFT]
          case NumberSubtype.Float64:
            return wasm.HEAPF64[ptr >> wasm.F64SHIFT]
          case NumberSubtype.Int8:
            return (unsigned ? wasm.HEAPU8 : wasm.HEAP8)[ptr]
          case NumberSubtype.Int16:
            return (unsigned ? wasm.HEAPU16 : wasm.HEAP16)[ptr >> wasm.INT16SHIFT]
          case NumberSubtype.Int32:
            return (unsigned ? wasm.HEAPU32 : wasm.HEAP32)[ptr >> wasm.INT32SHIFT]
          case NumberSubtype.Int64:
            return (unsigned ? wasm.HEAPU64 : wasm.HEAP64)[ptr >> wasm.INT64SHIFT]
        }
        break
      }
      case BindingType.Boolean:
        return wasm.HEAP8[ptr] ? true : false
      case BindingType.Struct: {
        const name = typeof typeName === 'string' ? typeName : binding.buildFullName()
        const cls = this.getBoundClass(this.get(name) as StructType<WASM>) as any
        return new cls(this.wasm, ptr, this)
      }
      case BindingType.Pointer:
      case BindingType.Reference: {
        const indirect = wasm.HEAPPTR[ptr >> wasm.PTRSHIFT]
        if (!indirect) {
          return undefined
        }
        const inner = (binding as any).ptrType as Binding | undefined
        if (inner === undefined) {
          return indirect
        }
        return this.getBoundPointer(inner, indirect)
      }
    }

    throw new Error('unknown binding type in getBoundPointer: ' + binding.type)
  }

  /**
   * note: this is equivalent to operator=(), the assignment operator.
   * if you want to copy construct, use the copy constructor instead.
   */
  setBoundPointer(typeName: string, ptr: pointer, value: any) {
    const wasm = this.wasm
    switch (typeName) {
      case 'float':
        wasm.HEAPF32[ptr >> wasm.F32SHIFT] = value as number
        break
      case 'double':
        wasm.HEAPF64[ptr >> wasm.F64SHIFT] = value as number
        break
      case 'char':
        wasm.HEAP8[ptr] = value as number
        break
      case 'short':
        wasm.HEAP16[ptr >> wasm.INT16SHIFT] = value as number
        break
      case 'int':
        wasm.HEAP32[ptr >> wasm.INT32SHIFT] = value as number
        break
      case 'longlong':
        wasm.HEAP64[ptr >> wasm.INT64SHIFT] = BigInt(value)
        break
      case 'uchar':
        wasm.HEAPU8[ptr] = value as number
        break
      case 'ushort':
        wasm.HEAPU16[ptr >> wasm.INT16SHIFT] = value as number
        break
      case 'uint':
        wasm.HEAPU32[ptr >> wasm.INT32SHIFT] = value as number
        break
      case 'ulonglong':
        wasm.HEAPU64[ptr >> wasm.INT64SHIFT] = BigInt(value)
        break
      default:
        break
    }

    const type = this.types.get(typeName)
    if (type instanceof StructType) {
      const otherPtr = typeof value === 'number' ? value : (value.ptr as number)
      if (otherPtr === ptr) {
        //self-assignment, no nothing
        return
      }

      const cpyCtor = type.findCopyConstructor()
      if (!cpyCtor) {
        throw new UnknownConstructorError('no copy constructor for ' + typeName)
      }

      this.wasm.LSTL_Destructor_Invoke(type.ptr, ptr)
      cpyCtor.constructTo(ptr, otherPtr)
    }
  }

  get<K extends keyof AllBoundTypes>(typeName: K): Binding
  get<K extends keyof AllBoundTypes | string>(typeName: K): Binding | undefined {
    return this.types.get(typeName as string) as Binding | undefined
  }

  getStruct<K extends keyof AllBoundTypes>(typeName: K): StructType<WASM, AllBoundTypes[K]>
  getStruct<K extends keyof AllBoundTypes | string>(typeName: K): StructType | undefined {
    const type = this.types.get(typeName as string) as StructType | undefined
    if (type !== undefined && type.type !== BindingType.Struct) {
      throw new NotStructError(`Type ${String(typeName)} is not a struct`)
    }
    return type
  }

  construct<K extends keyof AllBoundTypes>(typeName: K): AllBoundTypes[K] {
    const type = this.get(typeName) as StructType<WASM> | undefined

    if (!type) {
      throw new UnknownTypeError(`Type ${String(typeName)} not found`)
    }

    // find default constructor
    const ctype = type.findDefaultConstructor()
    if (!ctype) {
      throw new UnknownConstructorError(`No default constructor found for type ${String(typeName)}`)
    }

    return this.constructWith(ctype)
  }

  invokeMethod(instance: WasmBase<WASM>, structType: StructType, methodType: MethodType, ...args: any[]) {
    const ptr = instance.ptr
    const resultPtr = methodType.invoke(ptr, ...args)
    const returnType = methodType.returnType

    if (returnType === undefined || resultPtr === undefined) {
      return undefined
    }

    return this.getBoundPointer(returnType, resultPtr)
  }

  constructWith<K extends keyof AllBoundTypes>(ctype: ConstructorType<WASM>, ...args: any[]): AllBoundTypes[K] {
    const cls = this.getBoundClass(ctype.ownerType) as any
    const ptr = ctype.construct(...args)
    return new cls(this.wasm, ptr, this)
  }

  constructClass<ARGS extends unknown[] = unknown[]>(ctype: ConstructorType<WASM>, ...args: ARGS) {
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    const cls = this.getBoundClass(ctype.ownerType) as any
    const ptr = ctype.construct(...args)
    return new cls(this.wasm, ptr, this, ctype.ownerType)
  }

  destroyInstance(structType: StructType<WASM>, instance: WasmBase<WASM>) {
    const wasm = this.wasm
    wasm.LSTL_Destructor_Invoke(structType.ptr, instance.ptr)
    wasm.memRelease(instance.ptr)
  }

  load() {
    const wasm = this.wasm
    const keysPtr = wasm.LSTL_Binding_GetKeys(this.ptr)
    const keys = wasm.jsString(keysPtr)

    wasm.LSTL_Binding_FreeKeys(keysPtr)

    const types = keys.split('\\').filter((k) => k.length > 0)
    console.log(types)
    for (const key of types) {
      const ptr = wasm.LSTL_Binding_Get(this.ptr, wasm.cstring(key))
      if (ptr === 0) {
        throw new Error(`binding ${key} not found`)
      }
      const type = getBinding(wasm, ptr)
      this.types.set(key, type)
    }
  }

  generateTypeScript() {
    const wasm = this.wasm
    const ptr = wasm.memAlloc(wasm.cstring('generateTypeScript temp'), 4)

    const bufPtr = wasm.LSTL_GenerateTypescript(this.ptr, ptr)
    const size = wasm.HEAP32[ptr >> wasm.INT32SHIFT]

    let i = bufPtr

    let intHelper = new Int32Array(1)
    let u8 = new Uint8Array(intHelper.buffer)
    let heap = wasm.HEAPU8

    const files = new Map<string, string>()
    function readInt() {
      for (let j = 0; j < 4; j++) {
        u8[j] = heap[i++]
      }
      return intHelper[0]
    }

    while (i < bufPtr + size) {
      const keySize = readInt()
      let key = ''

      for (let j = 0; j < keySize; j++) {
        key += String.fromCharCode(heap[i++])
      }

      let file = ''
      const fileSize = readInt()
      for (let j = 0; j < fileSize; j++) {
        file += String.fromCharCode(heap[i++])
      }

      files.set(key, file)
    }

    wasm.memRelease(ptr)
    wasm.LSTL_FreeTypescriptString(bufPtr)
    return files
  }
}

export type BindingManagerAny = BindingManager<any, any>
