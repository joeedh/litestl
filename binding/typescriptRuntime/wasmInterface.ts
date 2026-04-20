export type pointer<T = any> = number
type int = number
type float = number
type bool = number
type size_t = number
type cstring = pointer<number>

interface IWasmBase {
    LSTL_GetBindingInfo(): pointer
    LSTL_FreeBindingInfo(info: pointer): void
    HEAPU8: Uint8Array;

    // tagged allocators, tags are to help track down leaks
    memAlloc(tag: cstring, size: number): pointer
    memRelease(ptr: number): void

    // non-tagged 
    _rawAlloc(size: number): pointer
    _rawRelease(ptr: pointer): void
}

export interface IBindingInfo {
    Offsets: {
        Base: {
            name: number
            type: number
        }
        Struct: {
            members: number
            methods: number
            templateParams: number
            structSize: number
        }
    }
    Sizes: {
        Struct: {
            StructMember: number
            StructBase: number
            TemplateParam: number
            StructMethod: number
        }
    }
}

export interface INeededWasm extends IWasmBase {
    // convert js string to cstring, might want to use a string pool for this
    cstring(s: string): cstring

    // convert cstring to js string
    jsString(s: cstring): string

    LSTL_Binding_GetKeys(bindingManager: pointer): cstring
    LSTL_Binding_FreeKeys(bindingManager: pointer): cstring
    LSTL_Binding_Get(bindingManager: pointer, key: cstring): pointer;
    LSTL_Struct_GetMethodCount(structptr: pointer): size_t
    LSTL_Struct_GetMethod(structptr: pointer, i: size_t): pointer
    LSTL_Method_GetParamCount(method: pointer): size_t
    LSTL_Method_GetParam(method: pointer, i: size_t, outName: pointer<cstring>): pointer
    LSTL_Method_GetReturn(method: pointer): pointer
    LSTL_Method_GetName(method: pointer): cstring
    LSTL_Method_IsConst(method: pointer): int
    LSTL_Method_Invoke(method: pointer, self: pointer, args: pointer<pointer>, retbuf: pointer): void

    // various helper stuff, create with createWasmHelpers() below
    bindingInfo: IBindingInfo

    HEAPU16: Uint16Array;
    HEAPU32: Uint32Array;
    HEAP8: Int8Array;
    HEAP16: Int16Array;
    HEAP32: Int32Array;
    HEAPF32: Float64Array;
    HEAPF64: Float64Array;
    DATAVIEW: DataView;
    HEAPPTR: ArrayLike<number>;

    INT8SIZE: number,
    INT8SHIFT: number,
    INT16SIZE: number,
    INT16SHIFT: number,
    INT32SIZE: number,
    INT32SHIFT: number,
    PTRSIZE: number,
    PTRSHIFT: number,
    F32SIZE: number,
    F32SHIFT: number,
    F64SIZE: number,
    F64SHIFT: number
    SIZET_SIZE: number,
    SIZET_SHIFT: number,
}

export function createWasmViews(wasmBase: IWasmBase) {
    const array = wasmBase.HEAPU8.buffer
    return {
        HEAP16: new Int16Array(array),
        HEAP32: new Int32Array(array),
        HEAPU16: new Uint16Array(array),
        HEAPU32: new Uint32Array(array),
        HEAPPTR: new Uint32Array(array),
        HEAPF32: new Float32Array(array),
        HEAPF64: new Float64Array(array),
        ...wasmBase
    }
}

export function createWasmHelpers(wasmBase: IWasmBase) {
    const wasm = createWasmViews(wasmBase)
    let ptr = wasm.LSTL_GetBindingInfo()

    let i = ptr >> 2
    let data = wasm.HEAP32
    const bindingInfo: IBindingInfo = {
        Offsets: {
            Base: {
                name: data[i++],
                type: data[i++],
            },
            Struct: {
                members: data[i++],
                methods: data[i++],
                templateParams: data[i++],
                structSize: data[i++],
            }
        },
        Sizes: {
            Struct: {
                StructMember: data[i++],
                StructBase: data[i++],
                TemplateParam: data[i++],
                StructMethod: data[i++],
            }
        }
    }
    wasm.LSTL_FreeBindingInfo(ptr)

    const strPool = new Map<string, pointer>

    return {
        ...wasm,
        cstring(s: string) {
            let ptr = strPool.get(s)
            if (ptr !== undefined) {
                return ptr
            }

            ptr = wasm._rawAlloc(s.length + 1)
            const data = wasm.HEAPU8
            for (let i = 0; i < s.length; i++) {
                data[ptr + i] = s.charCodeAt(i)
            }
            data[ptr + s.length] = 0

            strPool.set(s, ptr)
            return ptr
        },
        jsString(ptr: pointer) {
            const data = this.HEAPU8
            let s = ''
            while (data[ptr]) {
                s += String.fromCharCode(data[ptr])
                ptr++
            }
            return s
        },
        HEAPPTR: wasm.HEAPU32,
        INT8SIZE: 1,
        INT8SHIFT: 0,
        INT16SIZE: 2,
        INT16SHIFT: 1,
        INT32SIZE: 4,
        INT32SHIFT: 2,
        PTRSIZE: 4,
        PTRSHIFT: 2,
        F32SIZE: 4,
        F32SHIFT: 2,
        F64SIZE: 8,
        F64SHIFT: 2,
        SIZET_SIZE: 4,
        SIZET_SHIFT: 2,
        bindingInfo
    }
}