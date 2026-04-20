import { INeededWasm } from './wasmInterface'
export class WasmBase<WASM extends INeededWasm = INeededWasm> {
    wasm: WASM
    ptr: number
    constructor(wasm: WASM, ptr: number) {
        this.wasm = wasm;
        this.ptr = ptr;
    }
}
