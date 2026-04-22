import { WasmBase } from './wasmBase';
import { INeededWasm, pointer } from './wasmInterface'
import {
  BindingBase, getBinding, BindingType
} from './binding'

export class BindingManager<WASM extends INeededWasm = INeededWasm> extends WasmBase<WASM> {
  types = new Map<string, BindingBase>()

  constructor(wasm: WASM, ptr: pointer) {
    super(wasm, ptr)
  }

  load() {
    const wasm = this.wasm
    const bi = wasm.bindingInfo
    const keysPtr = wasm.LSTL_Binding_GetKeys(this.ptr)
    const keys = wasm.jsString(keysPtr)

    wasm.LSTL_Binding_FreeKeys(keysPtr)

    const types = keys.split('|').filter(k => k.length > 0)
    console.log('keys', types)

    for (const key of types) {
      const ptr = wasm.LSTL_Binding_Get(this.ptr, wasm.cstring(key))
      const type = getBinding(wasm, ptr)
      this.types.set(key, type)
    }
  }

  generateTypeScript() {
    const wasm = this.wasm
    const ptr = wasm.memAlloc(wasm.cstring('generateTypeScript temp'), 4);

    const bufPtr = wasm.LSTL_GenerateTypescript(this.ptr, ptr);
    const size = wasm.HEAP32[ptr >> wasm.INT32SHIFT]

    let i = bufPtr;

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
        key += String.fromCharCode(heap[i++]);
      }

      let file = ''
      const fileSize = readInt()
      for (let j = 0; j < fileSize; j++) {
        file += String.fromCharCode(heap[i++]);
      }

      files.set(key, file)
    }

    wasm.memRelease(ptr);
    wasm.LSTL_FreeTypescriptString(bufPtr);
    return files
  }
}