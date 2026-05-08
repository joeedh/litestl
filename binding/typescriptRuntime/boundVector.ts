import {ArrayType, Binding, BindingType, NumberFlags, NumberSubtype, NumberType, StructType} from './binding'
import {BindingManager} from './manager'
import {createSetFunc} from './setValue'
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

    let type = arrayType.elemType as Binding
    while (type.type == BindingType.ParentTemplateParam) {
      type = type.concreteType
    }
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

  static constructFromItems<T = any>(
    wasm: INeededWasm,
    manager: BindingManager,
    elemType: Binding,
    items: any[],
    staticSize?: number
  ) {
    const vecType = manager.findVectorClass(elemType.buildFullName(), staticSize)
    if (vecType === undefined) {
      throw new Error(`missing litestl::util::Vector binding for ${elemType.buildFullName()}`)
    }

    const ctor = vecType.findDefaultConstructor()!
    const ptr = wasm.memAlloc(wasm.cstring(vecType.name), vecType.structSize)
    wasm.LSTL_Constructor_Invoke(ctor.ptr, ptr, 0)

    // resize vector

    const resizeMethod = vecType.methods.find((m) => m.name === 'resize')!

    // store both argument list and resize parameter in a single allocation
    const arg = wasm._rawAlloc(wasm.PTRSIZE * 4)

    wasm.HEAPPTR[arg >> wasm.PTRSHIFT] = arg + wasm.PTRSIZE
    wasm.HEAPU32[(arg + wasm.PTRSIZE) >> wasm.INT32SHIFT] = items.length

    // I don't think we need to make storage for return type since it's void,
    // but might as well be safe.
    wasm.LSTL_Method_Invoke(resizeMethod.ptr, ptr, arg, arg + wasm.PTRSIZE * 2)

    const boundVec = new BoundVector<T>(wasm, ptr, vecType, manager) as any

    // assign items
    let i = 0
    for (const item of items) {
      boundVec.setUninitialized(i++, item)
    }
    wasm._rawRelease(arg)

    return boundVec as BoundVector<T>
  }

  constructor(wasm: INeededWasm, ptr: pointer, vecType: StructType, manager: MANAGER) {
    super(wasm, ptr)
    this.wasm = wasm
    this.manager = manager

    // prevent printing of wasm in console
    Object.defineProperty(this, 'wasm', {enumerable: false, configurable: true})
    // prevent printing of wasm in console
    Object.defineProperty(this, 'manager', {enumerable: false, configurable: true})

    let type = vecType.templateParams[0].type as Binding
    while (type.type == BindingType.ParentTemplateParam) {
      type = type.concreteType
    }

    const _vec = new WasmVector(wasm, ptr, type.size)

    const boundCache = new Map<number, T>()
    const typeFullName = type.buildFullName()
    const elemType = vecType.templateParams[0].type as Binding

    const getItemPtr = (i: number) => _vec.get(i)
    const {set, setUninitialized} = createSetFunc(wasm, manager, (i) => _vec.get(i), elemType)

    const proxy = new Proxy(this, {
      get: (target, prop) => {
        if (prop === 'length') {
          return _vec.length
        } else if (prop === 'setUninitialized') {
          return setUninitialized
        } else if (prop === 'set') {
          return set
        }

        if (prop === Symbol.iterator) {
          const len = _vec.length
          return function* () {
            for (let i = 0; i < len; i++) {
              yield (proxy as any)[i as any]
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
      set(target, p, newValue, receiver) {
        if (typeof p === 'symbol') {
          receiver[p] = newValue
          return true
        }
        return set(parseInt(p), newValue)
      },
    })
    return proxy
  }
}
