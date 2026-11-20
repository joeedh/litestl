/**
 * How a JS call picks among the instantiations of a function template. Must
 * agree with binding/binding_dispatch.h, which the TS generator and the N-API
 * runtime use.
 *
 * - Single: one candidate, nothing to choose.
 * - Infer:  every candidate has a different list of bound-struct parameters, so
 *           the bound objects passed in identify the instantiation. Value
 *           structs (strings, spans, vectors) arrive as plain JS values and
 *           don't count.
 * - Select: the caller passes a selector key as the first argument: the full
 *           names of the template arguments joined with ',' ("float",
 *           "float,int32").
 */
import {Binding, BindingType, MethodType, StructType, stripParentTemplate} from './binding'

export enum DispatchMode {
  Single,
  Infer,
  Select,
}

const valueStructs = new Set([
  'litestl::util::String',
  'litestl::util::CString',
  'litestl::util::Span',
  'litestl::util::Vector',
])

/** The bound struct a parameter carries, by value, pointer or reference.
 * Value structs (strings, spans, vectors) arrive as plain JS values, so they
 * can't identify an instantiation and don't count. */
export function paramStruct(t: Binding): StructType | undefined {
  if (t.type === BindingType.Pointer || t.type === BindingType.Reference) {
    t = t.ptrType as Binding
  }
  return t.type === BindingType.Struct && !valueStructs.has(t.name) ? t : undefined
}

export function selectorKey(m: MethodType, owner?: StructType): string {
  const parts: string[] = []
  for (const p of owner?.templateParams ?? []) {
    parts.push(p.type.buildFullName())
  }
  for (const p of m.templateParams) {
    parts.push(p.type.buildFullName())
  }
  return parts.join(',')
}

export function structSignature(m: MethodType): string {
  let sig = ''
  let any = false
  for (const p of m.methodArgs) {
    const st = paramStruct(p.type)
    sig += (st ? st.buildFullName() : '-') + '|'
    any = any || st !== undefined
  }
  return any ? sig : ''
}

export function dispatchMode(group: MethodType[]): DispatchMode {
  if (group.length <= 1) {
    return DispatchMode.Single
  }
  const sigs = new Set<string>()
  for (const m of group) {
    const sig = structSignature(m)
    if (sig === '' || sigs.has(sig)) {
      return DispatchMode.Select
    }
    sigs.add(sig)
  }
  return DispatchMode.Infer
}

/** One JS callable: a free function's or static's instantiations, or an
 * instance method's. */
export class CallGroup {
  mode: DispatchMode
  keys: string[]

  constructor(
    public name: string,
    public methods: MethodType[],
    /** Per method: the struct it belongs to, for selector keys of statics. */
    public owners: (StructType | undefined)[] = methods.map(() => undefined)
  ) {
    this.mode = dispatchMode(methods)
    this.keys = methods.map((m, i) => selectorKey(m, owners[i]))
  }

  /** Picks the instantiation for `args`, returning it and the arguments with
   * any selector key removed. */
  pick(args: unknown[], typeNameOf: (v: unknown) => string | undefined): [MethodType, unknown[]] {
    switch (this.mode) {
      case DispatchMode.Single:
        return [this.methods[0], args]
      case DispatchMode.Select: {
        const key = args[0]
        if (typeof key !== 'string') {
          throw new TypeError(`${this.name}: the first argument must be a template selector key`)
        }
        const i = this.keys.indexOf(key)
        if (i < 0) {
          throw new TypeError(`${this.name}: no instantiation for selector '${key}'`)
        }
        return [this.methods[i], args.slice(1)]
      }
      case DispatchMode.Infer: {
        for (const m of this.methods) {
          if (m.methodArgs.length !== args.length) {
            continue
          }
          const ok = m.methodArgs.every(({type}, k) => {
            const st = paramStruct(stripParentTemplate(type))
            if (!st) {
              return true
            }
            return typeNameOf(args[k]) === st.buildFullName()
          })
          if (ok) {
            return [m, args]
          }
        }
        throw new TypeError(`${this.name}: no instantiation matches the arguments' types`)
      }
    }
  }
}
