import {WasmBase} from './wasmBase'
import {INeededWasm, pointer} from './wasmInterface'
import {readLiteStlString} from './string'
import {WasmVector} from './vector'

export enum BindingType {
  Boolean = 1 << 0, //1
  Number = 1 << 1, //2
  Pointer = 1 << 2, //4
  Reference = 1 << 3, //8
  Struct = 1 << 4, //16
  Array = 1 << 5, //32
  Method = 1 << 6, //64
  Literal = 1 << 7, //128
  Constructor = 1 << 8, //256
}

export enum NumberSubtype {
  Int8 = 1 << 0,
  Int16 = 1 << 1,
  Int32 = 1 << 2,
  Int64 = 1 << 3,
  Float32 = 1 << 4,
  Float64 = 1 << 5,
}

export enum NumberFlags {
  None = 0,
  Unsigned = 1 << 0,
}

export enum LitType {
  Bool = 0,
  Number = 1,
  String = 2,
}

export class BindingBase<
  WASM extends INeededWasm = INeededWasm,
  TYPE extends BindingType = BindingType,
> extends WasmBase<WASM> {
  type: TYPE
  name: string
  size: number

  constructor(wasm: WASM, ptr: number) {
    super(wasm, ptr)
    const bi = wasm.bindingInfo

    Error.stackTraceLimit = 100000
    this.type = wasm.HEAP32[(ptr + bi.Offsets.Base.type) >> wasm.INT32SHIFT] as TYPE
    this.name = readLiteStlString(wasm, ptr + bi.Offsets.Base.name)
    this.size = wasm.LSTL_GetBindTypeSize(ptr)

    // do not enumerate wasm in console.log
    Object.defineProperty(this, 'wasm', {enumerable: false, configurable: true})
  }
}

export class BooleanType<WASM extends INeededWasm = INeededWasm> extends BindingBase<WASM, BindingType.Boolean> {
  constructor(wasm: WASM, ptr: pointer) {
    super(wasm, ptr)
  }
}

export class NumberType<WASM extends INeededWasm = INeededWasm> extends BindingBase<WASM, BindingType.Number> {
  subtype: NumberSubtype
  flags: NumberFlags

  constructor(wasm: WASM, ptr: pointer) {
    super(wasm, ptr)
    const o = wasm.bindingInfo.Offsets.Number
    this.subtype = wasm.HEAP32[(ptr + o.subtype) >> wasm.INT32SHIFT]
    this.flags = wasm.HEAP32[(ptr + o.flags) >> wasm.INT32SHIFT]
  }
}

export class ArrayType<WASM extends INeededWasm = INeededWasm> extends BindingBase<WASM, BindingType.Array> {
  elemType: BindingBase<WASM> | null
  arraySize: number

  constructor(wasm: WASM, ptr: pointer) {
    super(wasm, ptr)
    const o = wasm.bindingInfo.Offsets.Array
    const elemPtr = wasm.HEAPPTR[(ptr + o.arrayType) >> wasm.PTRSHIFT]
    this.elemType = elemPtr ? (getBinding(wasm, elemPtr) as BindingBase<WASM>) : null
    this.arraySize = wasm.HEAPU32[(ptr + o.arraySize) >> wasm.SIZET_SHIFT]
  }
}

export interface StructMember {
  name: string
  type: BindingBase
  offset: number
}

export class StructType<WASM extends INeededWasm = INeededWasm> extends BindingBase<WASM, BindingType.Struct> {
  _members: WasmVector<WASM>
  members: StructMember[] = []
  methods: WasmVector<WASM>
  _constructors: WasmVector<WASM>
  templateParams: WasmVector<WASM>
  structSize: number
  constructors: ConstructorType<WASM>[] = []

  constructor(wasm: WASM, ptr: pointer) {
    super(wasm, ptr)
    const o = wasm.bindingInfo.Offsets.Struct
    const sz = wasm.bindingInfo.Sizes.Struct
    this._members = new WasmVector(wasm, ptr + o.members, sz.StructMember)
    this.methods = new WasmVector(wasm, ptr + o.methods, wasm.PTRSIZE)
    this._constructors = new WasmVector(wasm, ptr + o.constructors, wasm.PTRSIZE)
    this.templateParams = new WasmVector(wasm, ptr + o.templateParams, sz.TemplateParam)
    this.structSize = wasm.HEAPU32[(ptr + o.structSize) >> wasm.SIZET_SHIFT]

    // load constructors
    for (const _ptr of this._constructors) {
      const cptr = wasm.HEAPPTR[_ptr >> wasm.PTRSHIFT]
      this.constructors.push(new ConstructorType(wasm, cptr, this))
    }

    // load members
    for (const _ptr of this._members) {
      const name = readLiteStlString(wasm, _ptr + wasm.bindingInfo.Offsets.StructMember.name)
      console.log(this.name, 'N', name)
      const offset = wasm.HEAP32[(_ptr + wasm.bindingInfo.Offsets.StructMember.offset) >> wasm.INT32SHIFT]
      const type = getBinding(
        wasm,
        wasm.HEAPPTR[(_ptr + wasm.bindingInfo.Offsets.StructMember.type) >> wasm.PTRSHIFT] as pointer
      )
      this.members.push({name, type, offset})
    }
  }
}

export class WasmConstructError extends Error {}

export class ConstructorType<WASM extends INeededWasm = INeededWasm> extends BindingBase<
  WASM,
  BindingType.Constructor
> {
  ownerType: StructType<WASM>
  _constructArgs: WasmVector<WASM>
  constructArgs: BindingBase<WASM>[] = []

  constructor(wasm: WASM, ptr: pointer, ownerType: StructType<WASM>) {
    super(wasm, ptr)
    this.ownerType = ownerType
    const o = wasm.bindingInfo.Offsets.Constructor
    const sz = wasm.bindingInfo.Sizes

    this._constructArgs = new WasmVector(wasm, ptr + o.params, sz.Constructor.ConstructorParam)
    for (const _ptr of this._constructArgs) {
      const argPtr = wasm.HEAPPTR[_ptr >> wasm.PTRSHIFT]
      this.constructArgs.push(getBinding(wasm, argPtr))
    }
  }

  construct(...args: unknown[]) {
    const wasm = this.wasm
    if (args.length !== this.constructArgs.length) {
      throw new WasmConstructError(`Expected ${this.constructArgs.length} arguments`)
    }

    const getHeap = (num: NumberType) => {
      const subtype = num.subtype
      const unsigned = num.flags & NumberFlags.Unsigned

      switch (subtype) {
        case NumberSubtype.Int8:
          return unsigned ? wasm.HEAPU8 : wasm.HEAP8
        case NumberSubtype.Int16:
          return unsigned ? wasm.HEAPU16 : wasm.HEAP16
        case NumberSubtype.Int32:
          return unsigned ? wasm.HEAPU32 : wasm.HEAP32
        case NumberSubtype.Int64:
          return unsigned ? wasm.HEAPU64 : wasm.HEAP64
      }
      throw new Error('unknown number type ' + subtype)
    }

    const ptrList = wasm.memAlloc(wasm.cstring('constructor arguments'), wasm.PTRSIZE * this.constructArgs.length)
    const ptrs = [] as pointer[]

    let heap = wasm.HEAPU8
    let index = 0
    for (const type of this.constructArgs) {
      // XXX calc size properly
      const argPtr = wasm.memAlloc(wasm.cstring('constructor argument'), 8)
      ptrs.push(argPtr)

      wasm.HEAPPTR[(ptrList >> wasm.PTRSHIFT) + index] = argPtr

      for (let i = 0; i < 8; i++) {
        heap[argPtr + i] = 0
      }

      switch (type.type) {
        case BindingType.Boolean:
          heap[argPtr] = args[index] ? 1 : 0
          break
        case BindingType.Number: {
          const num = args[index] as NumberType
          switch (num.subtype) {
            case NumberSubtype.Int8:
              getHeap(num)[argPtr] = args[index] as number
              break
            case NumberSubtype.Int16:
              getHeap(num)[argPtr >> wasm.INT16SHIFT] = args[index] as number
              break
            case NumberSubtype.Int32:
              getHeap(num)[argPtr >> wasm.INT32SHIFT] = args[index] as number
              break
            case NumberSubtype.Int64:
              getHeap(num)[argPtr >> wasm.INT64SHIFT] = args[index] as number
              break
            case NumberSubtype.Float32:
              wasm.HEAPF32[argPtr >> wasm.F32SHIFT] = args[index] as number
              break
            case NumberSubtype.Float64:
              wasm.HEAPF64[argPtr >> wasm.F64SHIFT] = args[index] as number
              break
          }
          break
        }
        case BindingType.Pointer:
        case BindingType.Reference: {
          let argPtr2: number | undefined | null
          if (typeof args[index] === 'object' && 'ptr' in (args[index] as object)) {
            argPtr2 = (args[index] as {ptr: number}).ptr
          } else if (typeof args[index] === 'number') {
            argPtr2 = args[index] as number
          } else {
            console.log(args[index], type)
            throw new Error('unknown argument type for value ' + args[index])
          }
          if (argPtr2 === null || argPtr2 === undefined) {
            argPtr2 = 0
          }
          wasm.HEAPPTR[argPtr >> wasm.PTRSHIFT] = argPtr2
        }
      }
      index++
    }

    const result = wasm.memAlloc(wasm.cstring(this.ownerType.name), this.ownerType.structSize)
    wasm.LSTL_Constructor_Invoke(this.ptr, result, ptrList)

    for (const ptr of ptrs) {
      wasm.memRelease(ptr)
    }
    wasm.memRelease(ptrList)

    return result
  }
}

export class LiteralType<
  WASM extends INeededWasm = INeededWasm,
  LIT_TYPE extends LitType = LitType,
> extends BindingBase<WASM, BindingType.Literal> {
  litType: LIT_TYPE
  litBind: BindingBase<WASM> | null

  constructor(wasm: WASM, ptr: pointer) {
    super(wasm, ptr)
    const o = wasm.bindingInfo.Offsets.Literal
    this.litType = wasm.HEAP32[(ptr + o.litType) >> wasm.INT32SHIFT] as LIT_TYPE
    const bindPtr = wasm.HEAPPTR[(ptr + o.litBind) >> wasm.PTRSHIFT]
    this.litBind = bindPtr ? (getBinding(wasm, bindPtr) as BindingBase<WASM>) : null
  }
}

export class NumLitType<WASM extends INeededWasm = INeededWasm> extends LiteralType<WASM, LitType.Num> {
  data: number

  constructor(wasm: WASM, ptr: pointer) {
    super(wasm, ptr)
    const o = wasm.bindingInfo.Offsets.NumLit
    this.data = wasm.HEAP32[(ptr + o.data) >> wasm.INT32SHIFT]
  }
}

export class BoolLitType<WASM extends INeededWasm = INeededWasm> extends LiteralType<WASM, LitType.Bool> {
  data: boolean

  constructor(wasm: WASM, ptr: pointer) {
    super(wasm, ptr)
    const o = wasm.bindingInfo.Offsets.BoolLit
    this.data = wasm.HEAP32[(ptr + o.data) >> wasm.INT32SHIFT] !== 0
  }
}

export class StrLitType<WASM extends INeededWasm = INeededWasm> extends LiteralType<WASM, LitType.String> {
  data: string

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
        case LitType.Bool:
          return new BoolLitType(wasm, ptr)
        case LitType.Number:
          return new NumLitType(wasm, ptr)
        case LitType.String:
          return new StrLitType(wasm, ptr)
        default:
          return new LiteralType(wasm, ptr)
      }
    }
    default:
      return new BindingBase(wasm, ptr)
  }
}

abstract class PointerTypeBase<
  WASM extends INeededWasm = INeededWasm,
  TYPE extends BindingType = BindingType,
> extends BindingBase<WASM, TYPE> {
  ptrType!: BindingBase<WASM>

  constructor(wasm: WASM, ptr: pointer) {
    super(wasm, ptr)
  }
}

export class PointerType<WASM extends INeededWasm = INeededWasm> //
  extends PointerTypeBase<WASM, BindingType.Pointer>
{
  constructor(wasm: WASM, ptr: pointer) {
    super(wasm, ptr)
    const o = wasm.bindingInfo.Offsets.Pointer
    this.ptrType = getBinding(wasm, ptr + o.ptrType)
  }
}
export class ReferenceType<WASM extends INeededWasm = INeededWasm> //
  extends PointerTypeBase<WASM, BindingType.Reference>
{
  constructor(wasm: WASM, ptr: pointer) {
    super(wasm, ptr)
    const o = wasm.bindingInfo.Offsets.Reference
    this.ptrType = getBinding(wasm, ptr + o.refType)
  }
}

export type Binding<WASM extends INeededWasm = INeededWasm> =
  | BooleanType<WASM>
  | NumberType<WASM>
  | ArrayType<WASM>
  | StructType<WASM>
  | ConstructorType<WASM>
  | NumLitType<WASM>
  | BoolLitType<WASM>
  | StrLitType<WASM>
  | PointerType<WASM>
  | ReferenceType<WASM>
