/**
 * Loads a litestl module built with plain clang + the wasi sysroot
 * (build_files/wasm-clang.cmake), no Emscripten. Provides the handful of WASI
 * calls libc makes, a shared memory, and the `lstl` imports litestl declares.
 * The result has the shape createWasmHelpers() expects.
 */
import type {IWasmBase} from './wasmInterface'

export interface WasiLoadOptions {
  /** Defaults to a fresh shared memory of initialPages/maximumPages. */
  memory?: WebAssembly.Memory
  initialPages?: number
  maximumPages?: number
  stdout?: (line: string) => void
  stderr?: (line: string) => void
}

export class WasiExit extends Error {
  constructor(public code: number) {
    super(`wasm module exited with code ${code}`)
  }
}

const ENOSYS = 52

export interface WasiModule extends IWasmBase {
  instance: WebAssembly.Instance
  /** Names of the module's function exports; createWasmHelpers wraps them so
   * heap views are refreshed after any call that grew memory. */
  __exportNames: string[]
  [k: string]: unknown
}

export async function instantiateWasi(bytes: BufferSource, opts: WasiLoadOptions = {}): Promise<WasiModule> {
  const module = await WebAssembly.compile(bytes)
  const memory =
    opts.memory ??
    new WebAssembly.Memory({initial: opts.initialPages ?? 256, maximum: opts.maximumPages ?? 16384, shared: true})

  const u8 = () => new Uint8Array(memory.buffer)
  const view = () => new DataView(memory.buffer)
  const decoder = new TextDecoder()
  const lineBuffers = new Map<number, string>()
  const out = (fd: number, text: string) => {
    const sink = fd === 2 ? (opts.stderr ?? console.error) : (opts.stdout ?? console.log)
    let buf = (lineBuffers.get(fd) ?? '') + text
    let nl: number
    while ((nl = buf.indexOf('\n')) >= 0) {
      sink(buf.slice(0, nl))
      buf = buf.slice(nl + 1)
    }
    lineBuffers.set(fd, buf)
  }

  const wasi: Record<string, (...args: any[]) => number | bigint | void> = {
    fd_write(fd: number, iovs: number, iovsLen: number, nwritten: number) {
      const dv = view()
      const heap = u8()
      let total = 0
      let text = ''
      for (let i = 0; i < iovsLen; i++) {
        const ptr = dv.getUint32(iovs + i * 8, true)
        const len = dv.getUint32(iovs + i * 8 + 4, true)
        // TextDecoder refuses views of shared memory; slice copies.
        text += decoder.decode(heap.slice(ptr, ptr + len))
        total += len
      }
      dv.setUint32(nwritten, total, true)
      out(fd, text)
      return 0
    },
    fd_close: () => 0,
    fd_seek(_fd: number, _offset: bigint, _whence: number, newOffset: number) {
      view().setBigUint64(newOffset, 0n, true)
      return 0
    },
    fd_fdstat_get(_fd: number, stat: number) {
      const dv = view()
      dv.setUint8(stat, 2) // character device
      dv.setUint16(stat + 2, 0, true)
      dv.setBigUint64(stat + 8, 0xffffffffffffffffn, true)
      dv.setBigUint64(stat + 16, 0xffffffffffffffffn, true)
      return 0
    },
    environ_sizes_get(count: number, size: number) {
      view().setUint32(count, 0, true)
      view().setUint32(size, 0, true)
      return 0
    },
    environ_get: () => 0,
    args_sizes_get(count: number, size: number) {
      view().setUint32(count, 0, true)
      view().setUint32(size, 0, true)
      return 0
    },
    args_get: () => 0,
    clock_time_get(_id: number, _precision: bigint, time: number) {
      const ns = BigInt(Math.round(performance.timeOrigin * 1e6)) + BigInt(Math.round(performance.now() * 1e6))
      view().setBigUint64(time, ns, true)
      return 0
    },
    random_get(buf: number, len: number) {
      const tmp = new Uint8Array(len)
      crypto.getRandomValues(tmp)
      u8().set(tmp, buf)
      return 0
    },
    sched_yield: () => 0,
    proc_exit(code: number) {
      throw new WasiExit(code)
    },
  }

  const lstl = {
    stackTrace(dst: number, maxLen: number) {
      const s = (new Error().stack ?? '').split('\n').slice(2).join('\n')
      const bytes = new TextEncoder().encode(s).subarray(0, maxLen - 1)
      const heap = u8()
      heap.set(bytes, dst)
      heap[dst + bytes.length] = 0
    },
  }

  const imports: WebAssembly.Imports = {env: {memory}}
  for (const imp of WebAssembly.Module.imports(module)) {
    if (imp.kind !== 'function') continue
    const ns = (imports[imp.module] ??= {}) as Record<string, unknown>
    if (imp.module === 'wasi_snapshot_preview1') {
      ns[imp.name] = wasi[imp.name] ?? (() => ENOSYS)
    } else if (imp.module === 'lstl') {
      ns[imp.name] = (lstl as Record<string, unknown>)[imp.name]
    } else if (imp.module === 'wasi' && imp.name === 'thread-spawn') {
      // No worker pool here: report failure and let pthread_create return EAGAIN.
      ns[imp.name] = () => -1
    }
  }

  const instance = await WebAssembly.instantiate(module, imports)
  const exports = instance.exports as Record<string, unknown>
  if (typeof exports._initialize === 'function') {
    ;(exports._initialize as () => void)()
  }

  const result: Record<string, unknown> = {
    instance,
    wasmMemory: memory,
    HEAPU8: u8(),
    __exportNames: [],
  }
  for (const [name, value] of Object.entries(exports)) {
    if (typeof value === 'function') {
      // Also under Emscripten's `_name` so code written against it works.
      result[name] = value
      result['_' + name] = value
      ;(result.__exportNames as string[]).push(name)
    }
  }
  return result as unknown as WasiModule
}
