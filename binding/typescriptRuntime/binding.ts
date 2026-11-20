import {WasmBase} from './wasmBase'
import {INeededWasm, pointer} from './wasmInterface'
import {readLiteStlString} from './string'
import {WasmVector} from './vector'
import type {BindingManager} from './manager'

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
  Enum = 1 << 9, //512
  Union = 1 << 10, //1024
  ParentTemplateParam = 1 << 11, //2048
}

export enum NumberSubtype {
  Int8 = 1 << 0, //1
  Int16 = 1 << 1, //2
  Int32 = 1 << 2, //4
  Int64 = 1 << 3, //8
  Float32 = 1 << 4, //16
  Float64 = 1 << 5, //32
}

export const NumberShifts = {
  [NumberSubtype.Int8]   : 0,
  [NumberSubtype.Int16]  : 1,
  [NumberSubtype.Int32]  : 2,
  [NumberSubtype.Int64]  : 3,
  [NumberSubtype.Float32]: 2,
  [NumberSubtype.Float64]: 3,
}

export const NumberSizes = {
  [NumberSubtype.Int8]   : 0,
  [NumberSubtype.Int16]  : 1,
  [NumberSubtype.Int32]  : 2,
  [NumberSubtype.Int64]  : 3,
  [NumberSubtype.Float32]: 2,
  [NumberSubtype.Float64]: 3,
}

export function getNumberHeap(wasm: INeededWasm, type: NumberSubtype, unsigned: boolean) {
  switch (type) {
    case NumberSubtype.Int8:
      return unsigned ? wasm.HEAPU8 : wasm.HEAP8
    case NumberSubtype.Int16:
      return unsigned ? wasm.HEAPU16 : wasm.HEAP16
    case NumberSubtype.Int32:
      return unsigned ? wasm.HEAPU32 : wasm.HEAP32
    case NumberSubtype.Int64:
      return unsigned ? wasm.HEAPU64 : wasm.HEAP64
    case NumberSubtype.Float32:
      return wasm.HEAPF32
    case NumberSubtype.Float64:
      return wasm.HEAPF64
  }
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
    bindingCache.set(ptr, this)

    const bi = wasm.bindingInfo

    Error.stackTraceLimit = 100000
    this.type = wasm.HEAP32[(ptr + bi.Offsets.Base.type) >> wasm.INT32SHIFT] as TYPE
    this.name = readLiteStlString(wasm, ptr + bi.Offsets.Base.name)
    this.size = wasm.LSTL_GetBindTypeSize(ptr)

    // do not enumerate wasm in console.log
    Object.defineProperty(this, 'wasm', {enumerable: false, configurable: true})
  }

  buildFullName() {
    const namePtr = this.wasm.LSTL_Binding_GetFullName(this.ptr)
    const result = this.wasm.jsString(namePtr)
    return result
  }
}

/** Represents a template parameter of a nested struct that's inherited from a parent. */
export class ParentTemplateParamType<WASM extends INeededWasm = INeededWasm> extends BindingBase<
  WASM,
  BindingType.ParentTemplateParam
> {
  templParamName: string
  /** how far up the parent template is in the type hierarchy, almost always 0 (first parent) */
  parentDepth: number
  concreteType: Binding

  constructor(wasm: WASM, ptr: pointer) {
    super(wasm, ptr)
    this.templParamName = readLiteStlString(wasm, ptr + wasm.bindingInfo.Offsets.ParentTemplateParam.templParamName)
    this.parentDepth = wasm.HEAP32[(ptr + wasm.bindingInfo.Offsets.ParentTemplateParam.parentDepth) >> wasm.INT32SHIFT]

    const cptr = wasm.HEAPPTR[(ptr + wasm.bindingInfo.Offsets.ParentTemplateParam.concreteType) >> wasm.PTRSHIFT]
    this.concreteType = getBinding(wasm, cptr) as Binding
  }
}

export class EnumType<WASM extends INeededWasm = INeededWasm> extends BindingBase<WASM, BindingType.Enum> {
  _items: WasmVector<WASM>
  items = new Map<string, number | bigint>()
  baseSize: number
  isBitMask: boolean

  constructor(wasm: WASM, ptr: pointer) {
    super(wasm, ptr)
    const o = wasm.bindingInfo.Offsets.Enum

    this._items = new WasmVector(wasm, ptr + o.items, wasm.bindingInfo.Sizes.Enum.EnumItem)
    this.baseSize = wasm.HEAP32[(ptr + o.baseSize) >> wasm.INT32SHIFT]
    this.isBitMask = wasm.HEAP8[ptr + o.isBitMask] !== 0

    for (const itemPtr of this._items) {
      const name = readLiteStlString(wasm, itemPtr)
      switch (this.baseSize) {
        case 2:
          this.items.set(name, wasm.HEAP16[(itemPtr + wasm.bindingInfo.Offsets.EnumItem.value) >> wasm.INT16SHIFT])
          break
        case 4:
          this.items.set(name, wasm.HEAP32[(itemPtr + wasm.bindingInfo.Offsets.EnumItem.value) >> wasm.INT32SHIFT])
          break
        case 8:
          this.items.set(name, wasm.HEAP64[(itemPtr + wasm.bindingInfo.Offsets.EnumItem.value) >> wasm.INT64SHIFT])
          break
      }
    }
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

  get unsigned() {
    return Boolean(this.flags & NumberFlags.Unsigned)
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

export class StructType<WASM extends INeededWasm = INeededWasm, BoundType = any> extends BindingBase<
  WASM,
  BindingType.Struct
> {
  /** note: type alias, this is never written to or read at runtime */
  declare boundType: BoundType

  _members: WasmVector<WASM>
  members: StructMember[] = []
  _methods: WasmVector<WASM>
  methods: MethodType<WASM>[] = []
  templateParams: {name: string; type: BindingBase<WASM>}[] = []
  _constructors: WasmVector<WASM>
  _templateParams: WasmVector<WASM>
  structSize: number
  constructors: ConstructorType<WASM>[] = []

  get isVector() {
    return this.name == 'litestl::util::Vector'
  }

  constructor(wasm: WASM, ptr: pointer) {
    super(wasm, ptr)
    const o = wasm.bindingInfo.Offsets.Struct
    const sz = wasm.bindingInfo.Sizes.Struct
    this._members = new WasmVector(wasm, ptr + o.members, sz.StructMember)
    this._methods = new WasmVector(wasm, ptr + o.methods, wasm.PTRSIZE)
    this._constructors = new WasmVector(wasm, ptr + o.constructors, wasm.PTRSIZE)
    this._templateParams = new WasmVector(wasm, ptr + o.templateParams, sz.TemplateParam)
    this.structSize = wasm.HEAPU32[(ptr + o.structSize) >> wasm.SIZET_SHIFT]

    // load constructors
    for (const _ptr of this._constructors) {
      const cptr = wasm.HEAPPTR[_ptr >> wasm.PTRSHIFT]
      this.constructors.push(new ConstructorType(wasm, cptr, this))
    }

    // load members
    for (const _ptr of this._members) {
      const name = readLiteStlString(wasm, _ptr + wasm.bindingInfo.Offsets.StructMember.name)
      const offset = wasm.HEAP32[(_ptr + wasm.bindingInfo.Offsets.StructMember.offset) >> wasm.INT32SHIFT]
      const type = getBinding(
        wasm,
        wasm.HEAPPTR[(_ptr + wasm.bindingInfo.Offsets.StructMember.type) >> wasm.PTRSHIFT] as pointer
      )
      this.members.push({name, type, offset})
    }

    // load template params
    for (const _ptr of this._templateParams) {
      const paramName = readLiteStlString(wasm, _ptr + wasm.bindingInfo.Offsets.TemplateParam.name)
      const paramType = getBinding(
        wasm,
        wasm.HEAPPTR[(_ptr + wasm.bindingInfo.Offsets.TemplateParam.type) >> wasm.PTRSHIFT] as pointer
      )
      this.templateParams.push({name: paramName, type: paramType})
    }

    // load methods
    for (const vecPtr of this._methods) {
      const argPtr = wasm.HEAPPTR[vecPtr >> wasm.PTRSHIFT]
      this.methods.push(getBinding(wasm, argPtr) as MethodType<WASM>)
    }
  }

  findConstructor(name: string): ConstructorType<WASM> | undefined {
    return this.constructors.find((c) => c.name === name)
  }

  findDefaultConstructor() {
    for (const c of this.constructors) {
      if (c.constructArgs.length === 0) {
        return c
      }
    }
    return undefined
  }

  findCopyConstructor() {
    for (const c of this.constructors) {
      if (c.constructArgs.length !== 1) {
        continue
      }
      const type = c.constructArgs[0].type as Binding
      if (type.type === BindingType.Reference && type.ptrType.buildFullName() === this.buildFullName()) {
        return c
      }
    }
    return undefined
  }
}

export class WasmConstructError extends Error {}
export class WasmInvokeError extends Error {}

export interface ConstructArg<WASM extends INeededWasm = INeededWasm> {
  name: string
  type: Binding
}
export interface MethodArg<WASM extends INeededWasm = INeededWasm> extends ConstructArg<WASM> {
  name: string
  type: Binding
}

/** Who frees a returned pointer; mirrors types::ReturnPolicy. */
export enum ReturnPolicy {
  Borrowed = 0,
  Owned = 1,
}

const encoder = new TextEncoder()

export function stripParentTemplate(t: Binding): Binding {
  while (t.type === BindingType.ParentTemplateParam) {
    t = t.concreteType
  }
  return t
}

function isArrayLike(v: unknown): v is ArrayLike<unknown> {
  return Array.isArray(v) || (ArrayBuffer.isView(v) && !(v instanceof DataView))
}

/** Writes a JS number/boolean/enum into a scalar of type `t` at `addr`. */
export function writeScalar(wasm: INeededWasm, t: Binding, addr: pointer, v: unknown, what: string) {
  switch (t.type) {
    case BindingType.Boolean:
      wasm.HEAPU8[addr] = v ? 1 : 0
      return
    case BindingType.Enum: {
      if (typeof v !== 'number') {
        throw new TypeError(`${what}: expected an enum value`)
      }
      switch (t.baseSize) {
        case 1:
          wasm.HEAP8[addr] = v
          return
        case 2:
          wasm.HEAP16[addr >> 1] = v
          return
        case 8:
          wasm.HEAP64[addr >> 3] = BigInt(v)
          return
        default:
          wasm.HEAP32[addr >> 2] = v
          return
      }
    }
    case BindingType.Number: {
      const unsigned = (t.flags & NumberFlags.Unsigned) !== 0
      if (t.subtype === NumberSubtype.Int64) {
        if (typeof v !== 'number' && typeof v !== 'bigint') {
          throw new TypeError(`${what}: expected a number or bigint`)
        }
        const big = typeof v === 'bigint' ? v : BigInt(Math.trunc(v))
        if (unsigned) {
          wasm.HEAPU64[addr >> 3] = BigInt.asUintN(64, big)
        } else {
          wasm.HEAP64[addr >> 3] = BigInt.asIntN(64, big)
        }
        return
      }
      if (typeof v !== 'number') {
        throw new TypeError(`${what}: expected a number`)
      }
      switch (t.subtype) {
        case NumberSubtype.Int8:
          ;(unsigned ? wasm.HEAPU8 : wasm.HEAP8)[addr] = v
          return
        case NumberSubtype.Int16:
          ;(unsigned ? wasm.HEAPU16 : wasm.HEAP16)[addr >> 1] = v
          return
        case NumberSubtype.Int32:
          ;(unsigned ? wasm.HEAPU32 : wasm.HEAP32)[addr >> 2] = v
          return
        case NumberSubtype.Float32:
          wasm.HEAPF32[addr >> 2] = v
          return
        case NumberSubtype.Float64:
          wasm.HEAPF64[addr >> 3] = v
          return
      }
      return
    }
  }
  throw new TypeError(`${what}: unsupported value type`)
}

/** A thunk argument array, plus the work to run once the call returns. */
export interface BuiltArgs {
  ptrList: pointer
  /** Copies non-const spans back to JS and frees the temporaries. */
  finish(): void
}

/**
 * Marshals JS values into the thunk ABI: args[i] points at the value for value
 * and reference parameters, and at a pointer-sized slot for pointer parameters
 * (see detail::thunkArg in binding_method.h).
 */
export function buildArgs(
  wasm: INeededWasm,
  manager: BindingManager,
  argTypes: ConstructArg[],
  args: unknown[],
  what: string
): BuiltArgs {
  const temps: pointer[] = []
  const after: (() => void)[] = []
  const alloc = (size: number) => {
    const n = Math.max(8, size)
    const p = wasm.memAlloc(0, n)
    wasm.HEAPU8.fill(0, p, p + n)
    temps.push(p)
    return p
  }
  const finish = () => {
    try {
      for (const f of after.reverse()) {
        f()
      }
    } finally {
      for (const p of temps) {
        wasm.memRelease(p)
      }
    }
  }

  const structArg = (st: StructType, a: unknown, label: string): pointer => {
    switch (st.name) {
      case 'litestl::util::String': {
        if (typeof a !== 'string') {
          throw new TypeError(`${label}: expected a string`)
        }
        const bytes = encoder.encode(a)
        const buf = alloc(bytes.length + 1)
        wasm.HEAPU8.set(bytes, buf)
        const p = alloc(st.structSize)
        wasm.LSTL_String_InitAt(p, buf, bytes.length)
        after.push(() => wasm.LSTL_String_DestroyAt(p))
        return p
      }
      case 'litestl::util::CString': {
        if (typeof a !== 'string') {
          throw new TypeError(`${label}: expected a string`)
        }
        const bytes = encoder.encode(a)
        const buf = alloc(bytes.length + 1)
        wasm.HEAPU8.set(bytes, buf)
        const p = alloc(wasm.PTRSIZE)
        wasm.HEAPPTR[p >> wasm.PTRSHIFT] = buf
        return p
      }
      case 'litestl::util::Span': {
        if (!isArrayLike(a)) {
          throw new TypeError(`${label}: expected an array or typed array`)
        }
        const elem = stripParentTemplate(st.templateParams[0].type as Binding)
        const readonly = (st.templateParams[1]?.type as BoolLitType | undefined)?.data ?? true
        const n = a.length
        const data = alloc(n * elem.size)
        for (let i = 0; i < n; i++) {
          writeScalar(wasm, elem, data + i * elem.size, a[i], label)
        }
        if (!readonly) {
          after.push(() => {
            const out = a as unknown as unknown[]
            for (let i = 0; i < n; i++) {
              out[i] = manager.getBoundPointer(elem, data + i * elem.size)
            }
          })
        }
        const p = alloc(wasm.PTRSIZE * 2)
        wasm.HEAPPTR[p >> wasm.PTRSHIFT] = data
        wasm.HEAPU32[(p >> wasm.PTRSHIFT) + 1] = n
        return p
      }
    }
    if (st.isVector && isArrayLike(a)) {
      const elem = stripParentTemplate(st.templateParams[0].type as Binding)
      const ctor = st.findDefaultConstructor()
      const resize = st.methods.find((m) => m.name === 'resize')
      if (!ctor || !resize) {
        throw new TypeError(`${label}: vector type cannot be built from an array`)
      }
      const vec = alloc(st.structSize)
      wasm.LSTL_Constructor_Invoke(ctor.ptr, vec, 0)
      after.push(() => wasm.LSTL_Destructor_Invoke(st.ptr, vec))
      const rargs = alloc(wasm.PTRSIZE * 2)
      wasm.HEAPPTR[rargs >> wasm.PTRSHIFT] = rargs + wasm.PTRSIZE
      wasm.HEAP32[(rargs >> 2) + 1] = a.length
      wasm.LSTL_Method_Invoke(resize.ptr, vec, rargs, 0)
      const data = wasm.HEAPPTR[vec >> wasm.PTRSHIFT]
      for (let i = 0; i < a.length; i++) {
        writeScalar(wasm, elem, data + i * elem.size, a[i], label)
      }
      return vec
    }
    if (typeof a === 'number') {
      // A raw pointer, as older callers pass.
      return a
    }
    if (typeof a === 'object' && a !== null && typeof (a as {ptr?: unknown}).ptr === 'number') {
      const bindType = (a as {bindType?: Binding}).bindType
      if (bindType && bindType.buildFullName() !== st.buildFullName()) {
        throw new TypeError(`${label}: expected a bound ${st.buildFullName()}`)
      }
      const p = (a as {ptr: number}).ptr
      if (!p) {
        throw new Error(`${label}: bound object was disposed`)
      }
      return p
    }
    throw new TypeError(`${label}: expected a bound ${st.buildFullName()}`)
  }

  try {
    const ptrList = alloc(wasm.PTRSIZE * Math.max(1, argTypes.length))
    argTypes.forEach(({name, type}, i) => {
      const label = `${what} argument '${name}'`
      const t = stripParentTemplate(type)
      const a = args[i]
      let argPtr: pointer
      switch (t.type) {
        case BindingType.Number:
        case BindingType.Boolean:
        case BindingType.Enum:
          argPtr = alloc(8)
          writeScalar(wasm, t, argPtr, a, label)
          break
        case BindingType.Struct:
          argPtr = structArg(t, a, label)
          break
        case BindingType.Reference: {
          const inner = stripParentTemplate(t.ptrType as Binding)
          if (inner.type !== BindingType.Struct) {
            throw new TypeError(`${label}: unsupported reference parameter`)
          }
          argPtr = structArg(inner, a, label)
          break
        }
        case BindingType.Pointer: {
          argPtr = alloc(wasm.PTRSIZE)
          const inner = stripParentTemplate(t.ptrType as Binding)
          if (a === null || a === undefined) {
            if (t.isNonNull) {
              throw new TypeError(`${label}: must not be null`)
            }
          } else if (typeof a === 'number') {
            wasm.HEAPPTR[argPtr >> wasm.PTRSHIFT] = a
          } else if (inner.type === BindingType.Struct) {
            wasm.HEAPPTR[argPtr >> wasm.PTRSHIFT] = structArg(inner, a, label)
          } else {
            throw new TypeError(`${label}: unsupported pointer parameter`)
          }
          break
        }
        default:
          throw new TypeError(`${label}: unsupported parameter type`)
      }
      wasm.HEAPPTR[(ptrList >> wasm.PTRSHIFT) + i] = argPtr
    })
    return {ptrList, finish}
  } catch (e) {
    finish()
    throw e
  }
}

export class MethodType<WASM extends INeededWasm = INeededWasm> extends BindingBase<WASM, BindingType.Method> {
  returnType?: Binding<WASM>
  _methodArgs: WasmVector<WASM>
  methodArgs: MethodArg<WASM>[] = []
  isConst: boolean
  isStatic: boolean
  returnPolicy: ReturnPolicy
  templateParams: {name: string; type: Binding<WASM>}[] = []
  private _jsName: string

  constructor(wasm: WASM, ptr: pointer) {
    super(wasm, ptr)
    const o = wasm.bindingInfo.Offsets
    const sz = wasm.bindingInfo.Sizes.Method

    const retPtr = wasm.HEAPPTR[(ptr + o.Method.returnType) >> wasm.PTRSHIFT]
    this.returnType = retPtr === 0 ? undefined : (getBinding(wasm, retPtr) as Binding<WASM>)
    this._methodArgs = new WasmVector(wasm, ptr + wasm.bindingInfo.Offsets.Method.params, sz.MethodParam)

    for (const _ptr of this._methodArgs) {
      const name = readLiteStlString(wasm, _ptr + wasm.bindingInfo.Offsets.MethodParam.name)
      const typePtr = _ptr + wasm.bindingInfo.Offsets.MethodParam.type
      const argPtr = wasm.HEAPPTR[typePtr >> wasm.PTRSHIFT]
      this.methodArgs.push({name, type: getBinding(wasm, argPtr) as Binding})
    }

    this.isConst = wasm.HEAPU8[ptr + o.Method.isConst] !== 0
    this.isStatic = wasm.HEAPU8[ptr + o.Method.isStatic] !== 0
    this.returnPolicy = wasm.HEAP32[(ptr + o.Method.returnPolicy) >> wasm.INT32SHIFT]
    this._jsName = readLiteStlString(wasm, ptr + o.Method.jsName)
    const tparams = new WasmVector(wasm, ptr + o.Method.templateParams, wasm.bindingInfo.Sizes.Struct.TemplateParam)
    for (const _ptr of tparams) {
      const name = readLiteStlString(wasm, _ptr + o.TemplateParam.name)
      const type = getBinding(wasm, wasm.HEAPPTR[(_ptr + o.TemplateParam.type) >> wasm.PTRSHIFT]) as Binding<WASM>
      this.templateParams.push({name, type})
    }
  }

  /** `name` without its namespace. */
  get shortName() {
    const i = this.name.lastIndexOf('::')
    return i < 0 ? this.name : this.name.slice(i + 2)
  }
  get jsName() {
    return this._jsName || this.shortName
  }
  get qualifiedJsName() {
    const i = this.name.lastIndexOf('::')
    return i < 0 ? this.jsName : this.name.slice(0, i) + '::' + this.jsName
  }

  /** Calls the thunk. Returns the return buffer, which the caller frees, or
   * undefined for void methods. */
  invoke(thisPtr: pointer, manager: BindingManager, ...args: unknown[]) {
    if (args.length !== this.methodArgs.length) {
      throw new TypeError(`${this.jsName}: expected ${this.methodArgs.length} arguments, got ${args.length}`)
    }
    const built = buildArgs(this.wasm, manager, this.methodArgs, args, this.jsName)
    let retBuf = 0
    try {
      if (this.returnType !== undefined) {
        const size = Math.max(8, this.returnType.size)
        retBuf = this.wasm.memAlloc(0, size)
        this.wasm.HEAPU8.fill(0, retBuf, retBuf + size)
      }
      this.wasm.LSTL_Method_Invoke(this.ptr, thisPtr, built.ptrList, retBuf)
    } finally {
      built.finish()
    }
    return retBuf === 0 ? undefined : retBuf
  }
}

export class ConstructorType<WASM extends INeededWasm = INeededWasm> extends BindingBase<
  WASM,
  BindingType.Constructor
> {
  ownerType: StructType<WASM>
  _constructArgs: WasmVector<WASM>
  constructArgs: ConstructArg<WASM>[] = []

  constructor(wasm: WASM, ptr: pointer, ownerType: StructType<WASM>) {
    super(wasm, ptr)
    this.ownerType = ownerType
    const o = wasm.bindingInfo.Offsets.Constructor
    const sz = wasm.bindingInfo.Sizes

    this._constructArgs = new WasmVector(wasm, ptr + o.params, sz.Constructor.ConstructorParam)
    for (const _ptr of this._constructArgs) {
      const name = readLiteStlString(wasm, _ptr + wasm.bindingInfo.Offsets.ConstructorParam.name)
      const typePtr = _ptr + wasm.bindingInfo.Offsets.ConstructorParam.type
      const argPtr = wasm.HEAPPTR[typePtr >> wasm.PTRSHIFT]
      this.constructArgs.push({name, type: getBinding(wasm, argPtr) as Binding})
    }
  }

  constructTo(thisPtr: number, manager: BindingManager, ...args: unknown[]) {
    const wasm = this.wasm
    if (args.length !== this.constructArgs.length) {
      throw new TypeError(
        `constructor '${this.name}' expects ${this.constructArgs.length} arguments, got ${args.length}`
      )
    }
    const built = buildArgs(wasm, manager, this.constructArgs, args, 'constructor')
    try {
      wasm.LSTL_Constructor_Invoke(this.ptr, thisPtr, built.ptrList)
    } finally {
      built.finish()
    }
    return thisPtr
  }

  construct(manager: BindingManager, ...args: unknown[]) {
    const wasm = this.wasm
    const result = wasm.memAlloc(wasm.cstring(this.ownerType.name), this.ownerType.structSize)
    try {
      this.constructTo(result, manager, ...args)
    } catch (e) {
      wasm.memRelease(result)
      throw e
    }
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

export class NumLitType<WASM extends INeededWasm = INeededWasm> extends LiteralType<WASM, LitType.Number> {
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

// note: to avoid infinite recursion,
// binding types must add themselves to
// bindingCache in their constructors
const bindingCache = new Map<pointer, BindingBase>()

export function getBinding<WASM extends INeededWasm = INeededWasm>(wasm: WASM, ptr: pointer): BindingBase<WASM> {
  const result = bindingCache.get(ptr)

  if (result) {
    return result as BindingBase<WASM>
  }

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
    case BindingType.Enum:
      return new EnumType(wasm, ptr)
    case BindingType.Method:
      return new MethodType(wasm, ptr)
    case BindingType.Union:
      return new UnionType(wasm, ptr)
    case BindingType.Pointer:
      return new PointerType(wasm, ptr)
    case BindingType.Reference:
      return new ReferenceType(wasm, ptr)
    case BindingType.ParentTemplateParam:
      return new ParentTemplateParamType(wasm, ptr)
    default:
      return new BindingBase(wasm, ptr)
  }
}

interface IUnionStruct {
  name: string
  struct: StructType
  typeValue: any
}

export class UnionType<WASM extends INeededWasm> extends BindingBase<WASM, BindingType.Union> {
  _structs: WasmVector<WASM>
  structs: IUnionStruct[] = []
  /** Disambiguation property name. */
  disPropName: string
  disPropType: Binding<WASM>

  /**
   * Does this union have a custom disambiguation property getter?
   * note: custom getters are forced to be int32 backed
   */
  get hasDisPropFunc() {
    return this.wasm.LSTL_Union_HasDisPropFunc(this.ptr)
  }

  getDisProp(manager: BindingManager, thisPtr: pointer) {
    if (this.hasDisPropFunc) {
      return this.wasm.LSTL_Union_RunDisPropFunc(this.ptr, thisPtr)
    }

    // TODO: read disambiguation property using this.disPropName and this.disPropType,
    // node that it's not required to be an integer (unlike the case with disPropFunc
    // callbacks).
  }

  constructor(wasm: WASM, ptr: pointer) {
    super(wasm, ptr)

    const o = wasm.bindingInfo.Offsets.Union
    const sz = wasm.bindingInfo.Sizes.Union

    this.disPropName = readLiteStlString(wasm, ptr + o.disPropName)
    this.disPropType = getBinding(wasm, wasm.HEAPPTR[(ptr + o.disPropType) >> wasm.PTRSHIFT]) as Binding<WASM>

    this._structs = new WasmVector(wasm, ptr + o.structs, sz.UnionPair)
    for (const sPtr of this._structs) {
      const name = readLiteStlString(wasm, sPtr + wasm.bindingInfo.Offsets.UnionPair.name)
      const structPtr = sPtr + wasm.bindingInfo.Offsets.UnionPair.type
      const struct = getBinding(wasm, wasm.HEAPPTR[structPtr >> wasm.PTRSHIFT]) as StructType<WASM>

      let typeValue: any | undefined
      const typePtr = sPtr + wasm.bindingInfo.Offsets.UnionPair.typeValue

      const disPropType = this.disPropType
      switch (disPropType.type) {
        case BindingType.Boolean:
          typeValue = wasm.HEAPU8[typePtr]
          break
        case BindingType.Number:
          typeValue = wasm.getNumHeap(disPropType.subtype, Boolean(disPropType.flags & NumberFlags.Unsigned))[
            typePtr >> wasm.getNumShift(disPropType.subtype)
          ]
          break
        case BindingType.Enum:
          let numType: number
          switch (disPropType.baseSize) {
            case 1:
              numType = NumberSubtype.Int8
              break
            case 2:
              numType = NumberSubtype.Int16
              break
            case 4:
              numType = NumberSubtype.Int32
              break
            case 8:
              numType = NumberSubtype.Int64
              break
            default:
              throw new Error('unknown enum base size ' + disPropType.baseSize)
          }
          typeValue = wasm.getNumHeap(numType, false)[typePtr >> wasm.getNumShift(numType)]
          break
        case BindingType.Struct:
          if (disPropType.name.startsWith('litestl::util::string')) {
            typeValue = readLiteStlString(wasm, typePtr)
          }
          break
      }

      if (typeValue === undefined) {
        throw new Error('could not load discriminated union property of type ' + BindingType[disPropType.type])
      }
      this.structs.push({name, struct, typeValue})
    }
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
  isNonNull: boolean

  constructor(wasm: WASM, ptr: pointer) {
    super(wasm, ptr)
    const o = wasm.bindingInfo.Offsets.Pointer
    this.ptrType = getBinding(wasm, wasm.HEAPPTR[(ptr + o.ptrType) >> wasm.PTRSHIFT])
    this.isNonNull = Boolean(wasm.HEAPU8[ptr + o.isNonNull])
  }
}
export class ReferenceType<WASM extends INeededWasm = INeededWasm> //
  extends PointerTypeBase<WASM, BindingType.Reference>
{
  constructor(wasm: WASM, ptr: pointer) {
    super(wasm, ptr)
    const o = wasm.bindingInfo.Offsets.Reference
    this.ptrType = getBinding(wasm, wasm.HEAPPTR[(ptr + o.refType) >> wasm.PTRSHIFT])
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
  | UnionType<WASM>
  | EnumType<WASM>
  | ParentTemplateParamType<WASM>
