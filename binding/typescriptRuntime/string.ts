import { WasmBase } from './wasmBase';
import { INeededWasm, pointer } from './wasmInterface'
export function readLiteStlString<WASM extends INeededWasm = INeededWasm>(wasm: WASM, ptr: pointer) {
    // dereference data
    const data = wasm.HEAPPTR[ptr >> wasm.PTRSHIFT];
    const size = wasm.HEAP32[(ptr + wasm.PTRSIZE) >> wasm.PTRSHIFT]
    let s = ''
    for (let i = 0; i < size; i++) {
        s += String.fromCharCode(wasm.HEAPU8[data + i])
    }
    return s
}
