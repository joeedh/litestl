import {WasmBase} from './wasmBase'
import {INeededWasm} from './wasmInterface'

export class WasmVector<WASM extends INeededWasm = INeededWasm> extends WasmBase {
  elemSize: number
  constructor(wasm: WASM, ptr: number, elemSize: number) {
    super(wasm, ptr)
    this.elemSize = elemSize

    // prevent printing of wasm in console
    Object.defineProperty(this, 'wasm', {enumerable: false, configurable: true})
  }

  get length() {
    const wasm = this.wasm
    return this.wasm.HEAPU32[(this.ptr + wasm.PTRSIZE) >> wasm.SIZET_SHIFT]
  }

  get(i: number) {
    const wasm = this.wasm
    const data = this.wasm.HEAPPTR[this.ptr >> wasm.PTRSHIFT]
    return data + i * this.elemSize
  }

  /** Returns pointers to elements */
  *[Symbol.iterator]() {
    const wasm = this.wasm
    const data = this.wasm.HEAPPTR[this.ptr >> wasm.PTRSHIFT]
    const elemSize = this.elemSize

    for (let i = 0; i < this.length; i++) {
      yield data + i * elemSize
    }
  }
}
