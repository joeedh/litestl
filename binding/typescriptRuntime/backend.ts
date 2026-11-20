/**
 * The JS-facing surface shared by the WASM runtime and the N-API addon
 * (binding/napi/napi_runtime.cc implements the same exports natively), so
 * generated TypeScript runs unchanged on either.
 */
import {BindingType, MethodType, StructType} from './binding'
import {CallGroup} from './dispatch'
import type {BindingManager} from './manager'

export interface IBindingBackend {
  readonly kind: string
  /** Default-constructs a struct; the result owns its object. */
  construct(typeName: string): any
  /** Constructs with the named constructor ("xy", "copy", ...). */
  constructWith(typeName: string, ctorName: string, ...args: unknown[]): any
  /** A free function by qualified JS name ("ns::name"). */
  getFunction(name: string): (...args: any[]) => any
  /** A static method of a struct (or of all instantiations of a template). */
  getStatic(structName: string, jsName: string): (...args: any[]) => any
  getClass(typeName: string): unknown
  /** Full type name of a bound object, or undefined for anything else. */
  typeName(obj: unknown): string | undefined
  bindingKeys(): string[]
  generateTypeScript(): Record<string, string>
  getMemSize(includePermanent?: boolean): number
}

let active: IBindingBackend | undefined

export function setBindingBackend(backend: IBindingBackend | undefined) {
  active = backend
}

export function getBindingBackend(): IBindingBackend {
  if (!active) {
    throw new Error('no binding backend: call setBindingBackend() first')
  }
  return active
}

/** A free function resolved against the active backend on first call, so
 * generated modules can be imported before a backend is set. */
export function bindFunction(name: string): (...args: any[]) => any {
  let backend: IBindingBackend | undefined
  let fn: ((...args: any[]) => any) | undefined
  return (...args: any[]) => {
    if (backend !== active || !fn) {
      backend = getBindingBackend()
      fn = backend.getFunction(name)
    }
    return fn(...args)
  }
}

export function bindStatic(structName: string, jsName: string): (...args: any[]) => any {
  let backend: IBindingBackend | undefined
  let fn: ((...args: any[]) => any) | undefined
  return (...args: any[]) => {
    if (backend !== active || !fn) {
      backend = getBindingBackend()
      fn = backend.getStatic(structName, jsName)
    }
    return fn(...args)
  }
}

/** IBindingBackend over a WASM module's BindingManager. */
export class WasmBindingBackend implements IBindingBackend {
  readonly kind = 'wasm'
  private functions = new Map<string, (...args: any[]) => any>()
  private statics = new Map<string, (...args: any[]) => any>()

  constructor(public manager: BindingManager) {
    const groups = new Map<string, MethodType[]>()
    for (const t of manager.types.values()) {
      if (t.type !== BindingType.Method) continue
      const m = t as MethodType
      const list = groups.get(m.qualifiedJsName) ?? []
      if (!list.includes(m)) list.push(m)
      groups.set(m.qualifiedJsName, list)
    }
    for (const [name, methods] of groups) {
      const group = new CallGroup(name, methods)
      this.functions.set(name, (...args: any[]) => manager.callGroup(group, 0, args))
    }
  }

  construct(typeName: string) {
    return this.manager.construct(typeName)
  }

  constructWith(typeName: string, ctorName: string, ...args: unknown[]) {
    const st = this.manager.types.get(typeName)
    if (!st || st.type !== BindingType.Struct) {
      throw new TypeError(`constructWith: unknown struct ${typeName}`)
    }
    const ctor = (st as StructType).findConstructor(ctorName)
    if (!ctor) {
      throw new TypeError(`constructWith: no constructor named ${ctorName}`)
    }
    return this.manager.constructWith(ctor, ...args)
  }

  getFunction(name: string) {
    const fn = this.functions.get(name)
    if (!fn) {
      throw new TypeError(`getFunction: unknown function ${name}`)
    }
    return fn
  }

  getStatic(structName: string, jsName: string) {
    const key = structName + '#' + jsName
    let fn = this.statics.get(key)
    if (fn) {
      return fn
    }
    const methods: MethodType[] = []
    const owners: StructType[] = []
    const seen = new Set<StructType>()
    for (const b of this.manager.types.values()) {
      const t = b as StructType
      if (t.type !== BindingType.Struct || t.name !== structName || seen.has(t)) continue
      seen.add(t)
      for (const m of t.methods) {
        if (m.isStatic && m.jsName === jsName) {
          methods.push(m)
          owners.push(t)
        }
      }
    }
    if (methods.length === 0) {
      throw new TypeError(`getStatic: no static ${jsName} on ${structName}`)
    }
    const group = new CallGroup(`${structName}.${jsName}`, methods, owners)
    fn = (...args: any[]) => this.manager.callGroup(group, 0, args)
    this.statics.set(key, fn)
    return fn
  }

  getClass(typeName: string) {
    const st = this.manager.types.get(typeName)
    if (!st || st.type !== BindingType.Struct) {
      throw new TypeError('getClass: unknown struct')
    }
    return this.manager.getBoundClass(st as StructType)
  }

  typeName(obj: unknown) {
    return this.manager.typeNameOf(obj)
  }

  bindingKeys() {
    return [...this.manager.types.keys()]
  }

  generateTypeScript() {
    return Object.fromEntries(this.manager.generateTypeScript())
  }

  getMemSize(includePermanent = false) {
    return this.manager.wasm.LSTL_GetMemSize(includePermanent)
  }
}
