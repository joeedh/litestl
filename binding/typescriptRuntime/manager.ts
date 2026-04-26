import {WasmBase} from './wasmBase'
import {INeededWasm, pointer} from './wasmInterface'
import {BindingBase, getBinding, BindingType, StructType, ConstructorType, ArrayType} from './binding'
import {createBoundType} from './bind'
import {BoundArray, BoundVector} from './boundVector'

export class BindingManager<
  WASM extends INeededWasm = INeededWasm,
  AllBoundTypes extends {[k: string]: unknown} = {},
> extends WasmBase<WASM> {
  types = new Map<string, BindingBase>()
  boundClasses = new Map<string, unknown>()
  //boundPointers = new Map<string, unknown>()

  constructor(wasm: WASM, ptr: pointer) {
    super(wasm, ptr)
    // do not enumerate this.wasm in console.log
    Object.defineProperty(this, 'wasm', {enumerable: false, configurable: true})
  }

  getBoundClass(type: StructType<WASM>) {
    let cls = this.boundClasses.get(type.name)
    if (cls) {
      return cls
    }

    cls = createBoundType(this, this.wasm, type)
    this.boundClasses.set(type.name, cls)
    return cls
  }

  getBoundArray(arrayTypeName: string, ptr: pointer) {
    const arrayType = this.types.get(arrayTypeName)
    if (arrayType === undefined) {
      throw new Error('unknown type ' + arrayTypeName)
    }
    return new BoundArray(this.wasm, ptr, arrayType as ArrayType, this)
  }

  getBoundVector(vecTypeName: string, ptr: pointer) {
    const vecType = this.types.get(vecTypeName)
    if (vecType === undefined) {
      throw new Error('unknown type ' + vecTypeName)
    }
    return new BoundVector(this.wasm, ptr, vecType as StructType, this)
  }

  getBoundPointer(typeName: string, ptr: pointer) {
    const wasm = this.wasm
    switch (typeName) {
      case 'float':
        return wasm.HEAPF32[ptr >> wasm.F32SHIFT]
      case 'double':
        return wasm.HEAPF64[ptr >> wasm.F64SHIFT]
      case 'char':
        return wasm.HEAP8[ptr]
      case 'short':
        return wasm.HEAP16[ptr >> wasm.INT16SHIFT]
      case 'int':
        return wasm.HEAP32[ptr >> wasm.INT32SHIFT]
      case 'longlong':
        return wasm.HEAP64[ptr >> wasm.INT64SHIFT]
      case 'uchar':
        return wasm.HEAPU8[ptr]
      case 'ushort':
        return wasm.HEAPU16[ptr >> wasm.INT16SHIFT]
      case 'uint':
        return wasm.HEAPU32[ptr >> wasm.INT32SHIFT]
      case 'ulonglong':
        return wasm.HEAPU64[ptr >> wasm.INT64SHIFT]
      default:
        break
    }
    const cls = this.getBoundClass(this.get(typeName) as StructType<WASM>) as any
    return new cls(this.wasm, ptr, this)
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
        throw new Error('no copy constructor for ' + typeName)
      }

      this.wasm.LSTL_Destructor_Invoke(type.ptr, ptr)
      cpyCtor.constructTo(ptr, otherPtr)
    }
  }

  construct<K extends keyof AllBoundTypes>(typeName: K): AllBoundTypes[K] {
    const type = this.get(typeName as string) as StructType<WASM> | undefined

    if (!type) {
      throw new Error(`Type ${String(typeName)} not found`)
    }

    // find default constructor
    const ctype = type.constructors.find((c) => c.constructArgs.length === 0)
    if (!ctype) {
      throw new Error(`No default constructor found for type ${String(typeName)}`)
    }

    const cls = this.getBoundClass(type) as any
    const ptr = ctype.construct()
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

    const types = keys.split('|').filter((k) => k.length > 0)

    for (const key of types) {
      const ptr = wasm.LSTL_Binding_Get(this.ptr, wasm.cstring(key))
      const type = getBinding(wasm, ptr)
      this.types.set(key, type)
    }
  }

  get(typeName: string) {
    return this.types.get(typeName)
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
