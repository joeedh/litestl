import {
  StructType,
  BindingBase,
  Binding,
  BindingType,
  NumberSubtype,
  NumberFlags,
  PointerType,
  ReferenceType,
} from './binding'
import type {INeededWasm} from './wasmInterface'
import {WasmBase} from './wasmBase'
import type {BindingManager} from './manager'
import {specialGenerators} from './specials'

interface IBoundClass {
  wasm: INeededWasm
  ptr: number
  getBoundPointer(typeName: string, ptr: number): unknown
  isBoundObject(obj: unknown): boolean
  manager: BindingManager
  bindType: Binding
  dispose(): void
}

class BoundClass extends WasmBase implements IBoundClass {
  manager: BindingManager
  bindType: Binding

  constructor(wasm: INeededWasm, ptr: number, manager: BindingManager, bindType: Binding<INeededWasm>) {
    super(wasm, ptr)
    this.manager = manager
    this.bindType = bindType
  }

  getBoundPointer(typeName: string, ptr: number): unknown {
    return this.manager.getBoundPointer(typeName, ptr)
  }

  isBoundObject(obj: unknown): obj is BoundClass {
    return typeof obj === 'object' && obj instanceof BoundClass
  }

  dispose(): void {
    this.manager.destroyInstance(this.bindType as StructType<INeededWasm>, this)
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
    case BindingType.Boolean:
      return {
        get: `this.wasm.HEAPU8[${ptrCode}] !== 0;`,
        set: `this.wasm.HEAPU8[${ptrCode}] = value ? 1 : 0;`,
      }
    case BindingType.Struct:
      // TODO: Implement struct binding
      return {
        get: `this.manager.getBoundPointer('${type.name}', ${ptrCode})`,
        set: `throw new Error("Setting embedded struct values is not supported")`,
      }
    case BindingType.Pointer:
    case BindingType.Reference: {
      if (type.ptrType?.type === BindingType.Struct) {
        return {
          get: `
            !this.wasm.HEAPPTR[${ptrCode} >> ${wasm.PTRSHIFT}] ? 
            undefined : 
            this.getBoundPointer('${type.ptrType.name}', this.wasm.HEAPPTR[${ptrCode} >> ${wasm.PTRSHIFT}])
          `,
          set: `
            this.wasm.HEAPPTR[${ptrCode} >> ${wasm.PTRSHIFT}] = value ? value.ptr : 0;
          `,
        }
      } else {
        const ptrCode2 = `this.wasm.HEAPPTR[${ptrCode} >> ${wasm.PTRSHIFT}]`
        return createBoundCode(manager, wasm, type.ptrType as Binding, ptrCode2, propKey)
      }
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

export function createBoundType(manager: BindingManager, wasm: INeededWasm, st: StructType) {
  const name = st.name.replace(/::/g, '_')
  let s = `class ${name} extends BoundClass {\n`
  let codePreSet = new Set<string>()

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

  console.log(s)
  s = `
  (function(BoundClass) {
    ${s};
    return ${name};
  })
  `

  return (0, eval)(s)(BoundClass)
}
