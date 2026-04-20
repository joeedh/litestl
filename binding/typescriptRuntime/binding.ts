import { WasmBase } from './wasmBase';
import { INeededWasm, pointer } from './wasmInterface'
import {
    readLiteStlString
} from './string'
import { WasmVector } from './vector'

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

export enum NumberSubtype {
    Int8 = 1 << 0,
    Int16 = 1 << 1,
    Int32 = 1 << 2,
    Int64 = 1 << 3,
    Float32 = 1 << 4,
    Float64 = 1 << 5,
};

export enum NumberFlags {
    None = 0,
    Unsigned = 1 << 0,
};

export enum LitType {
    Bool = 0,
    Number = 1,
    String = 2,
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

export class BooleanType<WASM extends INeededWasm = INeededWasm> extends BindingBase<WASM> {
    constructor(wasm: WASM, ptr: pointer) {
        super(wasm, ptr)
    }
}

export class NumberType<WASM extends INeededWasm = INeededWasm> extends BindingBase<WASM> {
    subtype: NumberSubtype;
    flags: NumberFlags;

    constructor(wasm: WASM, ptr: pointer) {
        super(wasm, ptr)
        const o = wasm.bindingInfo.Offsets.Number
        this.subtype = wasm.HEAP32[(ptr + o.subtype) >> wasm.INT32SHIFT]
        this.flags = wasm.HEAP32[(ptr + o.flags) >> wasm.INT32SHIFT]
    }
}

export class ArrayType<WASM extends INeededWasm = INeededWasm> extends BindingBase<WASM> {
    elemType: BindingBase<WASM> | null;
    arraySize: number;

    constructor(wasm: WASM, ptr: pointer) {
        super(wasm, ptr)
        const o = wasm.bindingInfo.Offsets.Array
        const elemPtr = wasm.HEAPPTR[(ptr + o.arrayType) >> wasm.PTRSHIFT]
        this.elemType = elemPtr ? getBinding(wasm, elemPtr) as BindingBase<WASM> : null
        this.arraySize = wasm.HEAPU32[(ptr + o.arraySize) >> wasm.SIZET_SHIFT]
    }
}

export class StructType<WASM extends INeededWasm = INeededWasm> extends BindingBase<WASM> {
    members: WasmVector<WASM>;
    methods: WasmVector<WASM>;
    templateParams: WasmVector<WASM>;
    structSize: number;

    constructor(wasm: WASM, ptr: pointer) {
        super(wasm, ptr)
        const o = wasm.bindingInfo.Offsets.Struct
        const sz = wasm.bindingInfo.Sizes.Struct
        this.members = new WasmVector(wasm, ptr + o.members, sz.StructMember)
        this.methods = new WasmVector(wasm, ptr + o.methods, wasm.PTRSIZE)
        this.templateParams = new WasmVector(wasm, ptr + o.templateParams, sz.TemplateParam)
        this.structSize = wasm.HEAPU32[(ptr + o.structSize) >> wasm.SIZET_SHIFT]
    }
}

export class LiteralType<WASM extends INeededWasm = INeededWasm> extends BindingBase<WASM> {
    litType: LitType;
    litBind: BindingBase<WASM> | null;

    constructor(wasm: WASM, ptr: pointer) {
        super(wasm, ptr)
        const o = wasm.bindingInfo.Offsets.Literal
        this.litType = wasm.HEAP32[(ptr + o.litType) >> wasm.INT32SHIFT]
        const bindPtr = wasm.HEAPPTR[(ptr + o.litBind) >> wasm.PTRSHIFT]
        this.litBind = bindPtr ? getBinding(wasm, bindPtr) as BindingBase<WASM> : null
    }
}

export class NumLitType<WASM extends INeededWasm = INeededWasm> extends LiteralType<WASM> {
    data: number;

    constructor(wasm: WASM, ptr: pointer) {
        super(wasm, ptr)
        const o = wasm.bindingInfo.Offsets.NumLit
        this.data = wasm.HEAP32[(ptr + o.data) >> wasm.INT32SHIFT]
    }
}

export class BoolLitType<WASM extends INeededWasm = INeededWasm> extends LiteralType<WASM> {
    data: boolean;

    constructor(wasm: WASM, ptr: pointer) {
        super(wasm, ptr)
        const o = wasm.bindingInfo.Offsets.BoolLit
        this.data = wasm.HEAP32[(ptr + o.data) >> wasm.INT32SHIFT] !== 0
    }
}

export class StrLitType<WASM extends INeededWasm = INeededWasm> extends LiteralType<WASM> {
    data: string;

    constructor(wasm: WASM, ptr: pointer) {
        super(wasm, ptr)
        const o = wasm.bindingInfo.Offsets.StrLit
        this.data = readLiteStlString(wasm, ptr + o.data)
    }
}

export function getBinding<WASM extends INeededWasm = INeededWasm>(wasm: WASM, ptr: pointer): BindingBase<WASM> {
    const bi = wasm.bindingInfo
    const type: BindingType = wasm.HEAP32[(ptr + bi.Offsets.Base.type) >> wasm.INT32SHIFT]

    switch (type) {
        case BindingType.Boolean:
            return new BooleanType(wasm, ptr)
        case BindingType.Number:
            return new NumberType(wasm, ptr)
        case BindingType.Array:
            return new ArrayType(wasm, ptr)
        case BindingType.Struct:
            return new StructType(wasm, ptr)
        case BindingType.Literal: {
            const litType: LitType = wasm.HEAP32[(ptr + bi.Offsets.Literal.litType) >> wasm.INT32SHIFT]
            switch (litType) {
                case LitType.Bool: return new BoolLitType(wasm, ptr)
                case LitType.Number: return new NumLitType(wasm, ptr)
                case LitType.String: return new StrLitType(wasm, ptr)
                default: return new LiteralType(wasm, ptr)
            }
        }
        default:
            return new BindingBase(wasm, ptr)
    }
}
