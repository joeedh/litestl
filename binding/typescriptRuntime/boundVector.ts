import {ArrayType, Binding, BindingType, StructType} from './binding'
import {BindingManager} from './manager'
import {WasmVector} from './vector'
import {WasmBase} from './wasmBase'
import {INeededWasm, pointer} from './wasmInterface'

export class BoundArray<T = any, MANAGER extends BindingManager = BindingManager> extends WasmBase {
  wasm: INeededWasm
  manager: MANAGER

  constructor(wasm: INeededWasm, ptr: pointer, arrayType: ArrayType, manager: MANAGER) {
    super(wasm, ptr)
    this.wasm = wasm
    this.manager = manager

    // prevent printing of wasm in console
    Object.defineProperty(this, 'wasm', {enumerable: false, configurable: true})
    // prevent printing of wasm in console
    Object.defineProperty(this, 'manager', {enumerable: false, configurable: true})

    const type = arrayType.elemType as Binding
    const elemSize = type.size

    const boundCache = new Map<number, T>()
    const typeFullName = type.buildFullName()

    return new Proxy(this, {
      get: (target, prop) => {
        if (prop === 'length') {
          return arrayType.arraySize
        }
        const i = parseInt(prop as string)
        if (isNaN(i)) {
          return target[prop as keyof this]
        }
        const itemPtr = ptr + i * elemSize

        if (type.type === BindingType.Struct) {
          const obj = boundCache.get(itemPtr)
          if (obj) {
            return obj
          }
          const cls = this.manager.getBoundClass(type) as any
          const bound = new cls(this.wasm, itemPtr, this.manager)
          boundCache.set(itemPtr, bound)
          return bound
        } else {
          return this.manager.getBoundPointer(type, itemPtr)
        }
      },
    })
  }
}

export class BoundVector<T = any, MANAGER extends BindingManager = BindingManager> extends WasmBase {
  wasm: INeededWasm
  manager: MANAGER

  constructor(wasm: INeededWasm, ptr: pointer, vecType: StructType, manager: MANAGER) {
    super(wasm, ptr)
    this.wasm = wasm
    this.manager = manager

    // prevent printing of wasm in console
    Object.defineProperty(this, 'wasm', {enumerable: false, configurable: true})
    // prevent printing of wasm in console
    Object.defineProperty(this, 'manager', {enumerable: false, configurable: true})

    const type = vecType.templateParams[0].type as Binding

    const _vec = new WasmVector(wasm, ptr, type.size)

    const boundCache = new Map<number, T>()
    const typeFullName = type.buildFullName()

    return new Proxy(this, {
      get: (target, prop) => {
        if (prop === 'length') {
          return _vec.length
        }
        if (prop === Symbol.iterator) {
          const len = _vec.length
          return function* () {
            for (let i = 0; i < len; i++) {
              yield (target as any)[i]
            }
          }
        }
        const i = parseInt(prop as string)
        if (isNaN(i)) {
          return target[prop as keyof this]
        }
        const itemPtr = _vec.get(i)

        if (type.type === BindingType.Struct) {
          const obj = boundCache.get(itemPtr)
          if (obj) {
            return obj
          }
          const cls = this.manager.getBoundClass(type) as any
          const bound = new cls(this.wasm, itemPtr, this.manager)
          boundCache.set(itemPtr, bound)
          return bound
        }
        return this.manager.getBoundPointer(type, itemPtr)
      },
    })
  }
}
