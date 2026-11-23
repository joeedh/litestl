/**
 * Foreign dispatch: C++ calling methods of JS objects (binding/foreign.h). A JS object
 * is registered for a handle (`foreignHandle`); C++ calls a method descriptor on the
 * handle, and `dispatch` below marshals the call with the descriptor's types.
 *
 * Everything lives on the module's `__lstlHooks` object, which `createWasmHelpers`
 * (Emscripten) or `instantiateWasi` creates before anything copies the module. The
 * Emscripten EM_JS functions and the WASI imports read it at call time, so one table
 * and one dispatch serve every BindingManager of the module.
 *
 * Rules (documentation/binding.md, "Foreign dispatch"):
 * - Non-owning wrappers given to the JS method, and wrappers reached through them, are
 *   valid for the call only: afterwards their `ptr` is 0 and accessors throw.
 * - Wrappers of const arguments are read-only.
 * - A trap or exit inside nested C++ (RuntimeError, WasiExit, ExitStatus) is rethrown
 *   and poisons the module: the frames it unwound left the C++ stack and state broken.
 */
import {Binding, BindingType, MethodType, StructType, getBinding, stripParentTemplate, writeScalar} from './binding'
import type {BindingManager} from './manager'
import {updateWasmViews, type INeededWasm} from './wasmInterface'

/** Per struct type name: what JS sees for an argument, and what is written for a return. */
export interface ForeignConverters {
  arg?: (value: unknown) => unknown
  ret?: (value: unknown) => unknown
}

/** Handles for JS objects. Counted; never reused. */
export class ForeignObjects {
  private next = 1
  private byHandle = new Map<number, {obj: object; count: number}>()
  private byObject = new WeakMap<object, number>()

  register(obj: object): number {
    if ((typeof obj !== 'object' && typeof obj !== 'function') || obj === null) {
      throw new TypeError('foreignHandle: expected an object')
    }
    const h = this.byObject.get(obj)
    const entry = h !== undefined ? this.byHandle.get(h) : undefined
    if (h !== undefined && entry) {
      entry.count++
      return h
    }
    const fresh = this.next++
    this.byHandle.set(fresh, {obj, count: 1})
    this.byObject.set(obj, fresh)
    return fresh
  }

  get(h: number): object | undefined {
    return this.byHandle.get(h)?.obj
  }

  release(h: number) {
    const entry = this.byHandle.get(h)
    if (entry && --entry.count <= 0) {
      this.byHandle.delete(h)
      this.byObject.delete(entry.obj)
    }
  }

  get size() {
    return this.byHandle.size
  }
}

export interface LstlHooks {
  foreign: ForeignObjects
  converters: Map<string, ForeignConverters>
  /** The manager whose descriptors dispatch reads (any of the module's will do). */
  manager?: BindingManager
  /** Set by a trap or exit during a foreign call. */
  poisoned?: unknown
  foreignCall?: (h: number, method: number, args: number, ret: number, err: number) => number
  foreignRelease?: (h: number) => void
}

export function createHooks(): LstlHooks {
  return {foreign: new ForeignObjects(), converters: new Map()}
}

/** The module's hooks object, created on `module` if it has none. Mutate it, never
 * replace it: EM_JS code and the WASI imports hold this object. */
export function hooksOf(module: object): LstlHooks {
  const m = module as {__lstlHooks?: LstlHooks}
  if (!m.__lstlHooks) {
    Object.defineProperty(m, '__lstlHooks', {value: createHooks(), enumerable: true, writable: false})
  }
  return m.__lstlHooks as LstlHooks
}

export function isPoisoned(wasm: object): boolean {
  return (wasm as {__lstlHooks?: LstlHooks}).__lstlHooks?.poisoned !== undefined
}

export function checkNotPoisoned(wasm: object) {
  if (isPoisoned(wasm)) {
    throw new Error('the wasm module was poisoned: a trap, an exit or an error escaped a foreign call')
  }
}

/* ---- scopes: wrappers valid for one call ---- */

interface Scoped {
  ptr: number
  owning?: boolean
  __scope?: Scoped[]
  __ro?: boolean
}

function hide(obj: object, key: string, value: unknown) {
  Object.defineProperty(obj, key, {value, enumerable: false, configurable: true, writable: true})
}

function addToScope(v: unknown, scope: Scoped[], ro: boolean) {
  if (typeof v !== 'object' || v === null || typeof (v as Scoped).ptr !== 'number') return
  const w = v as Scoped
  if (w.owning || w.__scope === scope) return
  hide(w, '__scope', scope)
  if (ro) hide(w, '__ro', true)
  scope.push(w)
}

/** Called by bound accessors and methods: what a scoped wrapper returns joins its scope. */
export function inheritScope(parent: {__scope?: object[]; __ro?: boolean}, v: unknown) {
  if (parent.__scope !== undefined && v !== parent) {
    addToScope(v, parent.__scope as Scoped[], parent.__ro === true)
  }
}

/* ---- dispatch ---- */

const encoder = new TextEncoder()

const isThenable = (v: unknown) =>
  (typeof v === 'object' || typeof v === 'function') && v !== null && typeof (v as {then?: unknown}).then === 'function'

/** A trap or exit inside C++: rethrown, never turned into an error result. */
function isFatal(e: unknown) {
  if (e instanceof WebAssembly.RuntimeError) return true
  const name = (e as {name?: unknown} | null)?.name
  return name === 'ExitStatus' || name === 'WasiExit'
}

function refreshViews(wasm: INeededWasm) {
  const buffer = wasm.wasmMemory.buffer
  if (wasm.HEAPU8.byteLength !== buffer.byteLength) {
    updateWasmViews(wasm, buffer)
  }
}

function findMethod(obj: object, name: string): ((...a: unknown[]) => unknown) | undefined {
  const fn = (obj as Record<string, unknown>)[name]
  if (typeof fn !== 'function') return undefined
  return fn === (Object.prototype as Record<string, unknown>)[name] ? undefined : (fn as (...a: unknown[]) => unknown)
}

const isArrayLike = (v: unknown): v is ArrayLike<unknown> =>
  Array.isArray(v) || (ArrayBuffer.isView(v) && !(v instanceof DataView))

class Dispatch {
  wasm: INeededWasm
  scope: Scoped[] = []

  constructor(
    public manager: BindingManager,
    public hooks: LstlHooks,
    public what: string
  ) {
    this.wasm = manager.wasm
  }

  /** A JS value for the argument of type `type` at `addr` (thunk ABI). */
  readArg(type: Binding, addr: number): unknown {
    const t = stripParentTemplate(type)
    const wasm = this.wasm
    switch (t.type) {
      case BindingType.Number:
      case BindingType.Boolean:
      case BindingType.Enum:
        return this.manager.getBoundPointer(t, addr)
      case BindingType.Struct:
        return this.structValue(t, addr, false)
      case BindingType.Reference: {
        const inner = stripParentTemplate(t.ptrType as Binding)
        return inner.type === BindingType.Struct
          ? this.structValue(inner, addr, t.isConst)
          : this.manager.getBoundPointer(inner, addr)
      }
      case BindingType.Pointer: {
        const p = wasm.HEAPPTR[addr >> wasm.PTRSHIFT]
        if (!p) return null
        const inner = stripParentTemplate(t.ptrType as Binding)
        if (inner.type === BindingType.Struct) return this.structValue(inner, p, t.isConst)
        break
      }
    }
    throw new TypeError(`${this.what}: unsupported parameter type ${t.buildFullName()}`)
  }

  structValue(st: StructType, addr: number, ro: boolean): unknown {
    const wasm = this.wasm
    switch (st.name) {
      case 'litestl::util::String':
        return this.manager.readLiteStlString(addr)
      case 'litestl::util::CString': {
        const p = wasm.HEAPPTR[addr >> wasm.PTRSHIFT]
        return p ? wasm.jsString(p) : undefined
      }
    }
    if (st.isVector || st.name === 'litestl::util::Span') {
      const elem = stripParentTemplate(st.templateParams[0].type as Binding)
      const data = wasm.HEAPPTR[addr >> wasm.PTRSHIFT]
      const n = wasm.HEAPU32[(addr + wasm.PTRSIZE) >> 2]
      if (elem.name === 'uint8') {
        return wasm.HEAPU8.slice(data, data + n)
      }
      const out: unknown[] = []
      for (let i = 0; i < n; i++) {
        const at = data + i * elem.size
        out.push(elem.type === BindingType.Struct ? this.structValue(elem, at, ro) : this.manager.getBoundPointer(elem, at))
      }
      return out
    }
    const w = this.manager.getBoundPointer(st.buildFullName(), addr)
    addToScope(w, this.scope, ro)
    const conv = this.hooks.converters.get(st.buildFullName())?.arg
    return conv ? conv(w) : w
  }

  /** Constructs the return of type `type` in the uninitialized `ret` from `v`. */
  writeReturn(type: Binding, ret: number, v: unknown) {
    const t = stripParentTemplate(type)
    const wasm = this.wasm
    const label = `${this.what} return value`
    switch (t.type) {
      case BindingType.Number:
      case BindingType.Boolean:
      case BindingType.Enum:
        writeScalar(wasm, t, ret, v, label)
        return
      case BindingType.Pointer: {
        if (v === null || v === undefined) {
          if (t.isNonNull) throw new TypeError(`${label}: must not be null`)
          wasm.HEAPPTR[ret >> wasm.PTRSHIFT] = 0
          return
        }
        wasm.HEAPPTR[ret >> wasm.PTRSHIFT] = this.boundPtr(stripParentTemplate(t.ptrType as Binding), v, label)
        return
      }
      case BindingType.Struct:
        this.writeStruct(t, ret, v, label)
        return
    }
    throw new TypeError(`${label}: unsupported return type ${t.buildFullName()}`)
  }

  boundPtr(st: Binding, v: unknown, label: string): number {
    const bindType = (v as {bindType?: Binding} | null)?.bindType
    const p = (v as {ptr?: unknown} | null)?.ptr
    if (st.type !== BindingType.Struct || typeof p !== 'number' || bindType?.buildFullName() !== st.buildFullName()) {
      throw new TypeError(`${label}: expected a bound ${st.buildFullName()}`)
    }
    if (!p) throw new Error(`${label}: the bound object was disposed`)
    return p
  }

  writeStruct(st: StructType, ret: number, v: unknown, label: string) {
    const wasm = this.wasm
    if (st.name === 'litestl::util::String') {
      if (typeof v !== 'string') throw new TypeError(`${label}: expected a string`)
      const bytes = encoder.encode(v)
      const buf = wasm.memAlloc(0, bytes.length + 1)
      try {
        wasm.HEAPU8.set(bytes, buf)
        wasm.LSTL_String_InitAt(ret, buf, bytes.length)
      } finally {
        wasm.memRelease(buf)
      }
      return
    }
    if (st.isVector) {
      const elem = stripParentTemplate(st.templateParams[0].type as Binding)
      const scalar = elem.type === BindingType.Number || elem.type === BindingType.Boolean || elem.type === BindingType.Enum
      const ctor = st.findDefaultConstructor()
      const resize = st.methods.find((m) => m.name === 'resize')
      if (!scalar || !ctor || !resize) throw new TypeError(`${label}: ${st.buildFullName()} can't be returned from JS`)
      if (!isArrayLike(v)) throw new TypeError(`${label}: expected an array or typed array`)
      wasm.LSTL_Constructor_Invoke(ctor.ptr, ret, 0)
      try {
        const rargs = wasm.memAlloc(0, wasm.PTRSIZE * 2)
        try {
          wasm.HEAPPTR[rargs >> wasm.PTRSHIFT] = rargs + wasm.PTRSIZE
          wasm.HEAP32[(rargs >> 2) + 1] = v.length
          wasm.LSTL_Method_Invoke(resize.ptr, ret, rargs, 0)
        } finally {
          wasm.memRelease(rargs)
        }
        refreshViews(wasm)
        const data = wasm.HEAPPTR[ret >> wasm.PTRSHIFT]
        for (let i = 0; i < v.length; i++) {
          writeScalar(wasm, elem, data + i * elem.size, v[i], label)
        }
      } catch (e) {
        wasm.LSTL_Destructor_Invoke(st.ptr, ret) /* leave nothing constructed */
        throw e
      }
      return
    }
    const copy = st.findCopyConstructor()
    if (!copy) throw new TypeError(`${label}: ${st.buildFullName()} has no copy constructor`)
    this.boundPtr(st, v, label)
    copy.constructTo(ret, this.manager, v)
  }

  writeError(err: number, msg: string) {
    if (err) {
      this.manager.writeLiteStlString(err, msg)
    }
  }
}

/** A thrown value's message, as N-API's foreign dispatch words one it can't convert. */
function errorText(e: unknown): string {
  try {
    return String(e instanceof Error ? e.message : e)
  } catch {
    return "a value that can't be converted to a string"
  }
}

/**
 * The entry the C++ hooks call: 0 on success, 1 with `err` set on failure.
 *
 * Anything that escapes it unwinds the C++ frames that called in without running their
 * cleanup, so it poisons the module, whatever it is: a trap, or an error thrown while
 * reporting an error (near the JS stack limit, writing the error calls into wasm again).
 */
export function dispatch(hooks: LstlHooks, handle: number, methodPtr: number, args: number, ret: number, err: number): number {
  try {
    return dispatchCall(hooks, handle, methodPtr, args, ret, err)
  } catch (e) {
    hooks.poisoned ??= e ?? new Error('undefined escaped a foreign call')
    throw e
  }
}

function dispatchCall(hooks: LstlHooks, handle: number, methodPtr: number, args: number, ret: number, err: number): number {
  const manager = hooks.manager as BindingManager
  const wasm = manager.wasm
  refreshViews(wasm)
  const method = getBinding(wasm, methodPtr) as MethodType
  const d = new Dispatch(manager, hooks, method.jsName)
  if (hooks.poisoned !== undefined) {
    d.writeError(err, `${method.jsName}: the module was poisoned by an earlier escape from a foreign call`)
    return 1
  }
  const obj = hooks.foreign.get(handle)
  if (!obj) {
    d.writeError(err, `${method.jsName}: unknown foreign handle ${handle}`)
    return 1
  }
  const fn = findMethod(obj, method.jsName)
  if (!fn) {
    d.writeError(err, `${method.jsName}: not implemented`)
    return 1
  }
  try {
    const jsArgs = method.methodArgs.map((p, i) => d.readArg(p.type, wasm.HEAPPTR[(args >> wasm.PTRSHIFT) + i]))
    let v = fn.apply(obj, jsArgs)
    if (isThenable(v)) {
      throw new TypeError('returned a Promise: foreign methods are synchronous')
    }
    refreshViews(wasm)
    if (method.returnType !== undefined) {
      const rt = stripParentTemplate(method.returnType)
      const conv = rt.type === BindingType.Struct ? hooks.converters.get(rt.buildFullName())?.ret : undefined
      if (conv) v = conv(v)
      d.writeReturn(method.returnType, ret, v)
    }
    return 0
  } catch (e) {
    if (isFatal(e)) {
      hooks.poisoned = e
      throw e
    }
    const msg = errorText(e)
    d.writeError(err, msg.startsWith(method.jsName + ':') ? msg : `${method.jsName}: ${msg}`)
    return 1
  } finally {
    for (const w of d.scope) {
      w.ptr = 0
    }
  }
}

/** Makes `manager` the one the module's hooks dispatch through, if none is yet. */
export function installForeignDispatch(manager: BindingManager) {
  const hooks = hooksOf(manager.wasm)
  if (hooks.manager) return
  hooks.manager = manager
  hooks.foreignCall = (h, m, args, ret, err) => dispatch(hooks, h, m, args, ret, err)
  hooks.foreignRelease = (h) => hooks.foreign.release(h)
}
