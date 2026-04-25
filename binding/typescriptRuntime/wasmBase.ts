import {INeededWasm} from './wasmInterface'
export class WasmBase<WASM extends INeededWasm = INeededWasm> {
  wasm: WASM
  ptr: number
  constructor(wasm: WASM, ptr: number) {
    this.wasm = wasm
    this.ptr = ptr
    // do not enumerate this.wasm in console.log
    Object.defineProperty(this, 'wasm', {enumerable: false, configurable: true})
  }
}
