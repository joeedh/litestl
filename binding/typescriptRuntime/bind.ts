import {StructType, BindingBase} from './binding'
import {INeededWasm} from './wasmInterface'
import {WasmBase} from './wasmBase'

export function createBoundCode(wasm: INeededWasm, type: BindingBase, ptrCode: string) {
  //
}

export function createBoundType(wasm: INeededWasm, st: StructType) {
  const name = st.name.replace(/::/g, '_')
  let s = `class ${name} extends WasmBase {\n`

  for (const member of st.members) {
    s += createBoundCode(wasm, member.type, 'this.ptr + ' + member.offset)
  }

  s += '}\n'

  s = `
  (function()(WasmBase) {
    return ${s};
  })
  `

  return (0, eval)(s)(WasmBase)
}
