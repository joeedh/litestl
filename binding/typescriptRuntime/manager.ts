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
}