import {readFileSync} from 'node:fs'
import {createRequire} from 'node:module'
import {
  BindingManager,
  createWasmHelpers,
  instantiateWasi,
  WasmBindingBackend,
  type IBindingBackend,
} from '@litestl/typescript-runtime'

/** Loads the backend named by LSTL_BACKEND from the fixture at LSTL_FIXTURE. */
export async function loadBackend(): Promise<IBindingBackend> {
  const kind = process.env.LSTL_BACKEND
  const file = process.env.LSTL_FIXTURE
  if (!file) {
    throw new Error('LSTL_FIXTURE is not set (run through run.mjs)')
  }
  if (kind === 'napi') {
    return createRequire(import.meta.url)(file) as IBindingBackend
  }
  if (kind === 'wasm') {
    const mod = await instantiateWasi(readFileSync(file))
    const wasm = createWasmHelpers(mod, undefined) as any
    const mgrPtr = wasm.LSTL_Fixture_GetManager()
    const error = wasm.LSTL_Manager_CheckFunctions(mgrPtr)
    if (error) {
      throw new Error(wasm.jsString(error))
    }
    const manager = new BindingManager(wasm, mgrPtr)
    manager.load()
    return new WasmBindingBackend(manager)
  }
  throw new Error(`LSTL_BACKEND must be wasm or napi, not ${kind}`)
}
