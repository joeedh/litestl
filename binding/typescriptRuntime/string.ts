import {WasmBase} from './wasmBase'
import {INeededWasm, pointer} from './wasmInterface'
const decoder = new TextDecoder()

export function readLiteStlString<WASM extends INeededWasm = INeededWasm>(wasm: WASM, ptr: pointer) {
  // dereference data
  const data = wasm.HEAPPTR[ptr >> wasm.PTRSHIFT]
  const size = wasm.HEAP32[(ptr + wasm.PTRSIZE) >> wasm.PTRSHIFT]
  // TextDecoder rejects views of shared memory; slice() copies.
  return decoder.decode(wasm.HEAPU8.slice(data, data + size))
}
