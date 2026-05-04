import * as binding from '@litestl/typescript-runtime'
const wasmURL = process.argv[2] ?? '../../../build/sculptcore.js'
const _wasm = await import(wasmURL)
const wasmMod = await _wasm.default({wasmMemory: binding.createWasmMemory()})

function setupWasm(wasm: any) {
  for (const k in wasm) {
    if (typeof k === 'string' && k[0] == '_') {
      wasm[k.slice(1)] = wasm[k]
    }
  }

  const wasm2 = binding.createWasmHelpers(wasm, _wasm.default)
  return wasm2 as unknown as binding.INeededWasm
}

const wasm = setupWasm(wasmMod)
console.log(wasm)
console.log(JSON.stringify(wasm.bindingInfo, undefined, 2))

wasmMod._initBindings()
const managerPtr = wasmMod._getBindingManager()
const manager = new binding.BindingManager(wasm, managerPtr)
manager.load()
