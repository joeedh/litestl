import { WasmBase } from './wasmBase';
import { INeededWasm } from './wasmInterface'
import {
    readLiteStlString
} from './string'

export enum BindingType {
    Boolean = 1 << 0, //1
    Number = 1 << 1, //2
    Pointer = 1 << 2, //4
    Reference = 1 << 3, //8
    Struct = 1 << 4, //16
    Array = 1 << 5, //32
    Method = 1 << 6, //64
    Literal = 1 << 7, //128
};

export class BindingBase<WASM extends INeededWasm = INeededWasm> extends WasmBase<WASM> {
    type: BindingType;
    name: string;

    constructor(wasm: WASM, ptr: number) {
        super(wasm, ptr)
        const bi = wasm.bindingInfo

        this.type = wasm.HEAP32[(ptr + bi.Offsets.Base.type) >> wasm.INT32SHIFT]
        this.name = readLiteStlString(wasm, ptr + bi.Offsets.Base.name);
    }
}

// TODO: create subclass of Struct, Array, number classes, etc

export function getBinding(wasm: INeededWasm, ptr: number) {
    // TODO create prop types here
    return new BindingBase(wasm, ptr)
}