import {Binding, BindingType} from './binding'
import {BindingManager} from './manager'
import {INeededWasm} from './wasmInterface'

export interface ISpecialGenerator {
  onBind(
    manager: BindingManager,
    wasm: INeededWasm,
    type: Binding,
    ptrCode: string,
    propKey: string
  ): {get: string; set: string; codePre?: string} | undefined
}

const VectorBinding: ISpecialGenerator = {
  onBind(manager, wasm, type, ptrCode, propKey) {
    if (type.type == BindingType.Struct && type.name.startsWith('litestl::util::Vector')) {
      //s = `this.manager.getBoundPointer('${type.name}', ${ptrCode})`
      const subType = type.templateParams[0]
      console.log(type.templateParams)
      return {
        codePre: `
const boundVecSym = Symbol('boundVecSym');
function _getBoundVec(obj, key, typeName, ptr) {
    console.log('ptr2:', ptr)
    if (!obj[boundVecSym]) {
        obj[boundVecSym] = {};
    }
    if (!obj[boundVecSym][key]) {
        obj[boundVecSym][key] = obj.manager.getBoundVector(typeName, ptr);
    }
    return obj[boundVecSym][key];
}
        `,
        get    : `_getBoundVec(this, '${propKey}', '${type.buildFullName()}', ${ptrCode})`,
        set    : '',
      }
    }
  },
}

const ArrayBinding: ISpecialGenerator = {
  onBind(manager, wasm, type, ptrCode, propKey) {
    if (type.type == BindingType.Array) {
      const typeName = type.buildFullName()
      if (!manager.types.has(typeName)) {
        console.log('had to add array type to manager')
        manager.types.set(typeName, type)
      }
      return {
        codePre: `
const boundArraySym = Symbol('boundArraySym');
function _getBoundArray(obj, key, typeName, ptr) {
    if (!obj[boundArraySym]) {
        obj[boundArraySym] = {};
    }
    if (!obj[boundArraySym][key]) {
        obj[boundArraySym][key] = obj.manager.getBoundArray(typeName, ptr);
    }
    return obj[boundArraySym][key];
}
        `,
        get    : `_getBoundArray(this, '${propKey}', '${type.buildFullName()}', ${ptrCode})`,
        set    : '',
      }
    }
  },
}

export const specialGenerators: ISpecialGenerator[] = [VectorBinding, ArrayBinding]
