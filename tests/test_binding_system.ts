import fs from 'fs'
import Path from 'path'
import {fileURLToPath, pathToFileURL} from 'url'
import {BindingManager, createWasmHelpers, INeededWasm, pointer} from '@litestl/typescript-runtime'
import type * as TSTypes from './ts'

interface MyWasmExtra extends INeededWasm {
  main(): void
  getBindingManager(): pointer
  manager: BindingManager<INeededWasm, TSTypes.AllBoundTypes>
}
const __filename = fileURLToPath(import.meta.url)
const __dirname = Path.dirname(__filename)

async function loadWasm() {
  const pathFile = Path.join(__dirname, 'test_binding_system.path.txt')
  const wasmPath = fs.readFileSync(pathFile, 'utf8').trim()

  const mod = await import(pathToFileURL(wasmPath).href)
  const _wasm = await mod.default()

  const wasmBase = createWasmHelpers(_wasm) as unknown as MyWasmExtra
  wasmBase.manager = new BindingManager(wasmBase, wasmBase.getBindingManager())
  wasmBase.manager.load()
  return wasmBase
}

function generateTS(wasm: MyWasmExtra) {
  const ts = wasm.manager.generateTypeScript()
  for (const [path, file] of ts) {
    const finalPath = Path.join(__dirname, 'ts', path)
    fs.mkdirSync(Path.dirname(finalPath), {recursive: true})
    fs.writeFileSync(finalPath, file)
  }
}

async function main() {
  const wasm = await loadWasm()
  generateTS(wasm)

  console.log('constructing...')
  const vecTest = wasm.manager.construct("test::VecTest") as TSTypes.VecTest
  
  console.log(vecTest)
  console.log([...wasm.manager.types.keys()])
  
  console.log(vecTest.pos[0].vec[0], vecTest.pos[0].vec[1], vecTest.pos[0].vec[2])
  console.log('ptr1:', vecTest.pos.ptr, vecTest.flag)
  //console.log(wasm.manager)
}

main()
