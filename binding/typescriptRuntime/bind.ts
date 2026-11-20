import {
  StructType,
  BindingBase,
  Binding,
  BindingType,
  NumberSubtype,
  NumberFlags,
  PointerType,
  ReferenceType,
  MethodType,
} from './binding'
import type {INeededWasm} from './wasmInterface'
import {WasmBase} from './wasmBase'
import type {BindingManager, BindingManagerAny} from './manager'
import {specialGenerators} from './specials'
import {CallGroup} from './dispatch'

interface IBoundClass {
  wasm: INeededWasm
  ptr: number
  getBoundPointer(typeName: string, ptr: number): unknown
  isBoundObject(obj: unknown): boolean
  manager: BindingManager
  bindType: Binding
  [Symbol.dispose](): void
}

export interface IBoundWasmConstructor<T> {
  new (wasm: INeededWasm, ptr: number, manager: BindingManager, bindType: Binding<INeededWasm>, owning?: boolean): T
}

interface OwnedObject {
  manager: BindingManager
  st: StructType<INeededWasm>
  ptr: number
}

/** Destroys owning wrappers that were garbage collected without being disposed. */
const ownedRegistry = new FinalizationRegistry<OwnedObject>(({manager, st, ptr}) => {
  manager.destroyInstance(st, {ptr} as WasmBase)
})

class BoundClass extends WasmBase implements IBoundClass {
  manager: BindingManager
  bindType: Binding
  /** Owning wrappers destroy their object on dispose or garbage collection;
   * the others are views of memory owned elsewhere. */
  owning: boolean

  constructor(
    wasm: INeededWasm,
    ptr: number,
    manager: BindingManager,
    bindType: Binding<INeededWasm>,
    owning = false
  ) {
    super(wasm, ptr)
    this.manager = manager
    this.bindType = bindType
    this.owning = owning
    Object.defineProperty(this, 'manager', {enumerable: false, configurable: true})
    if (owning) {
      ownedRegistry.register(this, {manager, st: bindType as StructType<INeededWasm>, ptr}, this)
    }
  }

  getBoundPointer(typeName: string, ptr: number): unknown {
    return this.manager.getBoundPointer(typeName, ptr)
  }

  isBoundObject(obj: unknown): obj is BoundClass {
    return typeof obj === 'object' && obj instanceof BoundClass
  }

  [Symbol.dispose](): void {
    if (this.owning && this.ptr) {
      ownedRegistry.unregister(this)
      this.manager.destroyInstance(this.bindType as StructType<INeededWasm>, this)
    }
    this.owning = false
    this.ptr = 0
  }
}

export function createBoundCode(
  manager: BindingManager,
  wasm: INeededWasm,
  type: Binding,
  ptrCode: string,
  propKey: string
): {get: string; set: string; codePre?: string} | undefined {
  ptrCode = `(${ptrCode})`

  // Check for special generators first
  for (const generator of specialGenerators) {
    const result = generator.onBind(manager, wasm, type, ptrCode, propKey ?? '')
    if (result !== undefined) {
      return result
    }
  }

  switch (type.type) {
    case BindingType.ParentTemplateParam:
      return createBoundCode(manager, wasm, type.concreteType, ptrCode, propKey)
    case BindingType.Boolean:
      return {
        get: `this.wasm.HEAPU8[${ptrCode}] !== 0;`,
        set: `this.wasm.HEAPU8[${ptrCode}] = value ? 1 : 0;`,
      }
    case BindingType.Struct:
      // TODO: Implement struct binding
      return {
        get: `this.manager.getBoundPointer('${type.buildFullName()}', ${ptrCode})`,
        set: `throw new Error("Setting embedded struct values is not supported")`,
      }
    case BindingType.Pointer:
    case BindingType.Reference: {
      if (type.ptrType?.type === BindingType.Struct) {
        return {
          get: `(
            !this.wasm.HEAPPTR[${ptrCode} >> ${wasm.PTRSHIFT}] ? 
            undefined : 
            this.getBoundPointer('${type.ptrType.buildFullName()}', this.wasm.HEAPPTR[${ptrCode} >> ${wasm.PTRSHIFT}])
            )`,
          set: `
            this.wasm.HEAPPTR[${ptrCode} >> ${wasm.PTRSHIFT}] = value ? value.ptr : 0;
          `,
        }
      } else {
        const ptrCode2 = `this.wasm.HEAPPTR[${ptrCode} >> ${wasm.PTRSHIFT}]`
        return createBoundCode(manager, wasm, type.ptrType as Binding, ptrCode2, propKey)
      }
    }
    case BindingType.Enum: {
      switch (type.baseSize) {
        case 1:
          return {
            get: `this.wasm.HEAPU8[${ptrCode}]`,
            set: `this.wasm.HEAPU8[${ptrCode}] = value`,
          }
        case 2:
          return {
            get: `this.wasm.HEAPU16[${ptrCode} >> ${wasm.INT16SHIFT}]`,
            set: `this.wasm.HEAPU16[${ptrCode} >> ${wasm.INT16SHIFT}] = value`,
          }
        case 4:
          return {
            get: `this.wasm.HEAPU32[${ptrCode} >> ${wasm.INT32SHIFT}]`,
            set: `this.wasm.HEAPU32[${ptrCode} >> ${wasm.INT32SHIFT}] = value`,
          }
        case 8:
          return {
            get: `this.wasm.HEAPU64[${ptrCode} >> ${wasm.INT64SHIFT}]`,
            set: `this.wasm.HEAPU64[${ptrCode} >> ${wasm.INT64SHIFT}] = value`,
          }
        default:
          throw new Error(`Unsupported enum size: ${type.baseSize}`)
      }
      break
    }
    case BindingType.Number: {
      const s = type.flags & NumberFlags.Unsigned ? 'U' : ''

      switch (type.subtype) {
        case NumberSubtype.Int8:
          return {
            get: `this.wasm.HEAP${s}8[${ptrCode}];`,
            set: `this.wasm.HEAP${s}8[${ptrCode}] = value;`,
          }
        case NumberSubtype.Int16:
          return {
            get: `this.wasm.HEAP${s}16[${ptrCode} >> ${wasm.INT16SHIFT}];`,
            set: `this.wasm.HEAP${s}16[${ptrCode} >> ${wasm.INT16SHIFT}] = value;`,
          }
        case NumberSubtype.Int32:
          return {
            get: `this.wasm.HEAP${s}32[${ptrCode} >> ${wasm.INT32SHIFT}];`,
            set: `this.wasm.HEAP${s}32[${ptrCode} >> ${wasm.INT32SHIFT}] = value;`,
          }
        case NumberSubtype.Int64:
          return {
            get: `this.wasm.HEAP${s}64[${ptrCode} >> ${wasm.INT64SHIFT}];`,
            set: `this.wasm.HEAP${s}64[${ptrCode} >> ${wasm.INT64SHIFT}] = value;`,
          }
        case NumberSubtype.Float32:
          return {
            get: `this.wasm.HEAPF32[${ptrCode} >> ${wasm.F32SHIFT}];`,
            set: `this.wasm.HEAPF32[${ptrCode} >> ${wasm.F32SHIFT}] = value;`,
          }
        case NumberSubtype.Float64:
          return {
            get: `this.wasm.HEAPF64[${ptrCode} >> ${wasm.F64SHIFT}];`,
            set: `this.wasm.HEAPF64[${ptrCode} >> ${wasm.F64SHIFT}] = value;`,
          }
      }
    }
  }
}

export function createBoundType<T extends BindingManagerAny>(manager: T, wasm: INeededWasm, st: StructType) {
  const name = st.name.replace(/::/g, '_')
  let s =
    `
class ${name} extends BoundClass {
  bindType = structType; 
`.trim() + '\n'

  const codePreSet = new Set<string>()

  for (const member of st.members) {
    const memberCode = createBoundCode(
      manager,
      wasm,
      member.type as Binding,
      'this.ptr + ' + member.offset,
      member.name
    )
    if (memberCode === undefined) {
      console.log('Skipping member ' + member.name)
      continue
    }

    const {get, set, codePre} = memberCode
    if (codePre !== undefined) {
      codePreSet.add(codePre)
    }
    s += `
    get ${member.name}() {
      return ${get}
    }
    set ${member.name}(value) {
      ${set}
    }
    `
  }

  s += '\n}\n'
  s = Array.from(codePreSet).join('\n') + '\n' + s

  s = `
  (function(BoundClass, structType) {
    ${s};
    return ${name};
  })
  `

  try {
    const cls = (0, eval)(s)(BoundClass, st)
    addMethods(manager, st, cls)
    return cls
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
  } catch (error: any) {
    console.log(s)
    console.log(error.stack)
    console.log(error.message)
    console.log('complication failed')
    throw error
  }
}

/**
 * Instance methods, grouped by JS name: the instantiations of a template
 * method dispatch like free functions (see dispatch.ts); for plain overloads
 * the first registered wins, and members shadow methods, as in the N-API
 * runtime.
 */
function addMethods(manager: BindingManager, st: StructType, cls: {prototype: object}) {
  const memberNames = new Set(st.members.map((m) => m.name))
  const groups = new Map<string, MethodType[]>()
  for (const m of st.methods) {
    if (m.isStatic || memberNames.has(m.jsName)) continue
    const group = groups.get(m.jsName)
    if (!group) {
      groups.set(m.jsName, [m])
    } else if (m.templateParams.length > 0 && group[0].templateParams.length > 0) {
      group.push(m)
    }
  }
  for (const [name, methods] of groups) {
    const group = new CallGroup(name, methods)
    Object.defineProperty(cls.prototype, name, {
      configurable: true,
      writable: true,
      value: function (this: BoundClass, ...args: unknown[]) {
        if (!this.ptr) {
          throw new Error(`${name}: bound object was disposed`)
        }
        return manager.callGroup(group, this.ptr, args)
      },
    })
  }
}
