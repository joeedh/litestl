import {WasmBase} from './wasmBase'
import {INeededWasm, pointer} from './wasmInterface'
import {BindingBase, getBinding, BindingType, StructType, ConstructorType} from './binding'
import {createBoundType} from './bind'

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

  getBoundPointer(typeName: string, ptr: number) {
    const cls = this.getBoundClass(this.get(typeName) as StructType<WASM>) as any
    return new cls(this.wasm, ptr, this)
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
