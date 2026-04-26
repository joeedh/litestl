import fs from 'fs'
import Path from 'path'
import {fileURLToPath, pathToFileURL} from 'url'
import {BindingManager, createWasmHelpers, cstring, INeededWasm, pointer} from '@litestl/typescript-runtime'
import type * as TSTypes from './ts'
import {SnapShotManager} from './snapshotManager'

interface MyWasmExtra extends INeededWasm {
  main(): void
  GetMessageBuf(): cstring
  ClearMessageBuf(): void
  flushMessages(): string
  getBindingManager(): pointer
  snapshots: SnapShotManager
  snapshotMessages(snapshotName: string): void
  manager: BindingManager<INeededWasm, TSTypes.AllBoundTypes>
}
const __filename = fileURLToPath(import.meta.url)
const __dirname = Path.dirname(__filename)

let __wasm: MyWasmExtra | undefined

async function loadWasm() {
  if (__wasm !== undefined) {
    __wasm.snapshots.reset()
    return __wasm
  }
  const pathFile = Path.join(__dirname, 'test_binding_system.path.txt')
  const wasmPath = fs.readFileSync(pathFile, 'utf8').trim()

  const mod = await import(pathToFileURL(wasmPath).href)
  const _wasm = await mod.default()

  const wasmBase = createWasmHelpers(_wasm) as unknown as MyWasmExtra
  wasmBase.manager = new BindingManager(wasmBase, wasmBase.getBindingManager())
  wasmBase.manager.load()
  __wasm = wasmBase

  wasmBase.flushMessages = function () {
    const ptr = this.GetMessageBuf()
    if (!ptr) {
      return ''
    }
    const str = this.jsString(ptr)
    this.ClearMessageBuf()
    return str
  }

  wasmBase.snapshots = new SnapShotManager('test_binding_system')
  wasmBase.snapshotMessages = function (snapshotName) {
    const buf = this.flushMessages()
    this.snapshots.expect(snapshotName, buf)
  }
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

  wasm.ClearMessageBuf()
  let vecTest = wasm.manager.construct('test::VecTest') as TSTypes.VecTest
  wasm.snapshotMessages('test::VecTest default constructor')

  wasm.snapshots.log('wasm types', [...wasm.manager.types.keys()])

  wasm.snapshots.log('bound class accessors', vecTest.pos[0].vec[0], vecTest.pos[0].vec[1], vecTest.pos[0].vec[2])

  vecTest[Symbol.dispose]()

  const cls = wasm.manager.getStruct('test::VecTest')
  const ctor = cls.findConstructor('main')!

  vecTest = wasm.manager.constructWith<'test::VecTest'>(ctor, 1, 2.0, 3.0, true, false)
  wasm.snapshotMessages('test::VecTest argumented constructor')
  vecTest.print()
  wasm.snapshotMessages('test::VecTest print')
}

if (process.argv.includes('--repeat')) {
  setInterval(() => main(), 100)
} else {
  main()
}
