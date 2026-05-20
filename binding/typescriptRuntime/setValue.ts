import {Binding, BindingType, getNumberHeap, NumberFlags, NumberShifts, NumberSubtype, NumberType} from './binding'
import {BindingManager} from './manager'
import {INeededWasm, pointer} from './wasmInterface'

/** 
 I've written the binding read code to use generated code snippets
 that get compiled with eval(), but the binding write code (here) uses dynamically
 built functions created in a remarkably similar way.
 
 It might be worth investigating whether this approach would work for the
 read code as well.
*/

export function createSetFunc(
  wasm: INeededWasm,
  manager: BindingManager,
  getItemPtr: (index: number) => pointer,
  elemType: Binding
) {
  const umask = 1 << 30
  const calcUMask = (n: NumberType) => n.subtype | (n.flags & NumberFlags.Unsigned ? umask : 0)

  let setUninitialized: (index: number, value: any) => boolean
  let set: (index: number, value: any) => boolean

  switch (elemType.type) {
    case BindingType.Boolean:
      setUninitialized = set = (index, value) => {
        const itemPtr = getItemPtr(index)
        wasm.HEAPU8[itemPtr] = value ? 1 : 0
        return true
      }
      break
    case BindingType.Number: {
      if (elemType.subtype === NumberSubtype.Float32) {
        setUninitialized = set = (index, value) => {
          const itemPtr = getItemPtr(index)
          wasm.HEAPF32[itemPtr >> wasm.F32SHIFT] = value
          return true
        }
        break
      } else if (elemType.subtype === NumberSubtype.Float64) {
        setUninitialized = set = (index, value) => {
          const itemPtr = getItemPtr(index)
          wasm.HEAPF64[itemPtr >> wasm.F64SHIFT] = value
          return true
        }
        break
      }

      const heap = getNumberHeap(wasm, elemType.subtype, elemType.unsigned)
      const shift = NumberShifts[elemType.subtype]

      setUninitialized = set = (index, value) => {
        const itemPtr = getItemPtr(index)
        heap[itemPtr >> shift] = value
        return true
      }
      break
    }
    case BindingType.Pointer:
    case BindingType.Reference:
      setUninitialized = set = (index, value) => {
        const itemPtr = getItemPtr(index)
        wasm.HEAPPTR[itemPtr >> wasm.PTRSHIFT] = value
        return true
      }
      break
    case BindingType.Struct: {
      // find copy constructor
      const ctor = elemType.findCopyConstructor()
      if (ctor === undefined) {
        setUninitialized = set = (index, value) => {
          throw new Error('no copy constructor for ' + elemType.buildFullName())
        }
        break
      }

      setUninitialized = (index, value) => {
        const itemPtr = getItemPtr(index)
        const ptr = typeof value === 'object' ? value.ptr : value
        if (typeof ptr !== 'number') {
          console.warn(value)
          throw new Error('invalid element assignment for vector ' + elemType.buildFullName())
        }

        const arg = wasm._rawAlloc(wasm.PTRSIZE * 2)
        wasm.HEAPPTR[arg >> wasm.PTRSHIFT] = arg + wasm.PTRSIZE
        wasm.HEAPPTR[(arg >> wasm.PTRSHIFT) + 1] = ptr

        wasm.LSTL_Constructor_Invoke(ctor.ptr, itemPtr, arg)
        wasm._rawRelease(arg)
        return true
      }
      set = (index, value) => {
        const itemPtr = getItemPtr(index)
        wasm.LSTL_Destructor_Invoke(elemType.ptr, itemPtr)
        return setUninitialized(index, value)
      }
      break
    }
    default:
      setUninitialized = set = (index, value) => {
        throw new Error('cannot assign to vector ' + elemType.buildFullName())
      }
  }

  return {set, setUninitialized}
}
