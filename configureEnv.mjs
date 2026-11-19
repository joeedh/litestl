// Runs a command inside litestl's build environment.
//
//   node configureEnv.mjs <command...>      run <command> with the env applied
//   node configureEnv.mjs                   open a shell with the env applied
//   node configureEnv.mjs --output-env      print the env as KEY=VALUE lines
//   node configureEnv.mjs --fetch-deps      download the wasi sysroot + node-api headers
//
// On Windows the env is vcvars64 from the newest Visual Studio install (cl,
// cmake, ninja and VS's bundled clang all come from there). On Linux the system
// toolchain is used as-is. Both get LITESTL_DEPS_DIR pointing at the fetched
// dependencies (see --fetch-deps), which the CMake presets read.

import fs from 'fs'
import os from 'os'
import Path from 'path'
import child_process from 'child_process'
import {fileURLToPath} from 'url'

const ROOT = Path.dirname(fileURLToPath(import.meta.url))
const DEPS_DIR = process.env.LITESTL_DEPS_DIR || Path.join(ROOT, 'build', 'deps')

// Pinned so local builds and CI agree. The sysroot carries wasi-libc and
// libc++ for every wasm32 target; the builtins archive is compiler-rt, which
// VS's clang does not ship for wasm32.
const WASI_SDK_MAJOR = 25
const WASI_SDK_VERSION = `${WASI_SDK_MAJOR}.0`
const WASI_BASE = `https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-${WASI_SDK_MAJOR}`
const NODE_API_HEADERS_VERSION = '1.5.0'

const VSWHERE_DIR = Path.join(
  process.env['ProgramFiles(x86)'] || 'C:\\Program Files (x86)',
  'Microsoft Visual Studio',
  'Installer'
)

function getVSEnv() {
  const {join} = Path
  const VSPath = join(process.env.ProgramFiles, 'Microsoft Visual Studio')
  if (!fs.existsSync(VSPath)) {
    throw new Error('Visual Studio is not installed')
  }

  // Directory names are either release years (2022) or major versions (18).
  const versionMap = new Map([
    [2020, 16],
    [2022, 17],
    [2026, 18],
  ])
  const getVersion = (d) => versionMap.get(d) ?? d
  const versions = fs
    .readdirSync(VSPath)
    .filter((d) => !isNaN(parseInt(d)) && fs.statSync(join(VSPath, d)).isDirectory())
    .sort((a, b) => getVersion(parseInt(b)) - getVersion(parseInt(a)))

  const editions = ['Community', 'Professional', 'Enterprise', 'BuildTools', 'Preview']
  let vcvars
  for (const version of versions) {
    for (const ed of editions) {
      const cand = join(VSPath, version, ed, 'VC', 'Auxiliary', 'Build', 'vcvars64.bat')
      if (fs.existsSync(cand)) {
        vcvars = cand
        break
      }
    }
    if (vcvars) break
  }
  if (!vcvars) {
    throw new Error('could not find vcvars64.bat')
  }

  // vcvars rebuilds PATH itself and chokes on Git-Bash style entries, so give
  // it a minimal Windows PATH plus node (the binding tests run under it).
  const systemRoot = process.env.SystemRoot || 'C:\\Windows'
  const childEnv = {}
  for (const [k, v] of Object.entries(process.env)) {
    if (/^[A-Za-z_][A-Za-z0-9_()]*$/.test(k)) childEnv[k] = v
  }
  delete childEnv.MSYSTEM
  childEnv.PATH = [
    `${systemRoot}\\System32`,
    systemRoot,
    `${systemRoot}\\System32\\Wbem`,
    Path.dirname(process.execPath),
    fs.existsSync(join(VSWHERE_DIR, 'vswhere.exe')) ? VSWHERE_DIR : '',
  ]
    .filter(Boolean)
    .join(';')

  // `set` goes to a file, not a pipe: VS 2026's vcvars leaves a child holding
  // the inherited stdout handle, so a piped execSync never sees EOF.
  const tmp = fs.mkdtempSync(Path.join(os.tmpdir(), 'litestl-vcvars-'))
  const dumpPath = Path.join(tmp, 'env.txt')
  try {
    child_process.execSync(`cmd /s /c ""${vcvars}" && set > "${dumpPath}""`, {
      env: childEnv,
      stdio: 'ignore',
      timeout: 120000,
    })
    return fs
      .readFileSync(dumpPath, 'latin1')
      .split('\n')
      .filter((l) => !l.startsWith('**') && !l.trim().toLowerCase().startsWith('[vcvarsall.bat]'))
      .join('\n')
  } finally {
    fs.rmSync(tmp, {recursive: true, force: true})
  }
}

function applyEnv(text) {
  for (const line of text.replace(/\r/g, '').split('\n')) {
    const eq = line.indexOf('=')
    if (eq > 0) process.env[line.slice(0, eq)] = line.slice(eq + 1)
  }
}

async function download(url, dest) {
  const res = await fetch(url, {redirect: 'follow'})
  if (!res.ok) {
    throw new Error(`download failed (${res.status}): ${url}`)
  }
  fs.writeFileSync(dest, Buffer.from(await res.arrayBuffer()))
}

function untar(archive, destDir) {
  fs.mkdirSync(destDir, {recursive: true})
  // Windows ships bsdtar in System32. Name it explicitly: a Git-Bash GNU tar
  // earlier on PATH reads "C:\..." as a remote host.
  const tar =
    process.platform === 'win32' ? Path.join(process.env.SystemRoot || 'C:\\Windows', 'System32', 'tar.exe') : 'tar'
  child_process.execFileSync(tar, ['-xzf', archive, '-C', destDir], {stdio: 'inherit'})
}

async function fetchDeps() {
  fs.mkdirSync(DEPS_DIR, {recursive: true})
  const stamp = Path.join(DEPS_DIR, 'versions.json')
  const want = {wasiSdk: WASI_SDK_VERSION, nodeApiHeaders: NODE_API_HEADERS_VERSION}
  if (fs.existsSync(stamp) && JSON.stringify(JSON.parse(fs.readFileSync(stamp, 'utf8'))) === JSON.stringify(want)) {
    console.log(`deps already present in ${DEPS_DIR}`)
    return
  }

  const tmp = fs.mkdtempSync(Path.join(os.tmpdir(), 'litestl-deps-'))
  try {
    const sysroot = Path.join(DEPS_DIR, 'wasi-sysroot')
    const builtins = Path.join(DEPS_DIR, 'wasi-builtins')
    const napi = Path.join(DEPS_DIR, 'node-api-headers')
    for (const d of [sysroot, builtins, napi]) fs.rmSync(d, {recursive: true, force: true})

    console.log(`fetching wasi-sysroot ${WASI_SDK_VERSION}`)
    let a = Path.join(tmp, 'sysroot.tar.gz')
    await download(`${WASI_BASE}/wasi-sysroot-${WASI_SDK_VERSION}.tar.gz`, a)
    untar(a, Path.join(tmp, 'sysroot'))
    fs.renameSync(Path.join(tmp, 'sysroot', `wasi-sysroot-${WASI_SDK_VERSION}`), sysroot)

    console.log(`fetching wasm32 compiler-rt builtins ${WASI_SDK_VERSION}`)
    a = Path.join(tmp, 'builtins.tar.gz')
    await download(`${WASI_BASE}/libclang_rt.builtins-wasm32-wasi-${WASI_SDK_VERSION}.tar.gz`, a)
    untar(a, Path.join(tmp, 'builtins'))
    fs.renameSync(Path.join(tmp, 'builtins', `libclang_rt.builtins-wasm32-wasi-${WASI_SDK_VERSION}`), builtins)

    console.log(`fetching node-api-headers ${NODE_API_HEADERS_VERSION}`)
    a = Path.join(tmp, 'napi.tgz')
    await download(`https://registry.npmjs.org/node-api-headers/-/node-api-headers-${NODE_API_HEADERS_VERSION}.tgz`, a)
    untar(a, Path.join(tmp, 'napi'))
    fs.renameSync(Path.join(tmp, 'napi', 'package'), napi)

    fs.writeFileSync(stamp, JSON.stringify(want))
    console.log(`deps written to ${DEPS_DIR}`)
  } finally {
    fs.rmSync(tmp, {recursive: true, force: true})
  }
}

function quoteArg(a) {
  if (process.platform === 'win32') {
    return /[\s;,&|<>^()"]/.test(a) ? '"' + a.replace(/"/g, '""') + '"' : a
  }
  return /[^A-Za-z0-9_\/:=@%+.,-]/.test(a) ? "'" + a.replace(/'/g, "'\\''") + "'" : a
}

let args = process.argv.slice(2)

if (args[0] === '--fetch-deps') {
  await fetchDeps()
  process.exit(0)
}

let env = process.platform === 'win32' ? getVSEnv() : ''
env += `\nLITESTL_DEPS_DIR=${DEPS_DIR}`

if (args[0] === '--output-env') {
  process.stdout.write(env)
  process.exit(0)
}

applyEnv(env)

const shell = process.platform === 'win32' ? 'cmd' : process.env.SHELL || '/bin/sh'
try {
  child_process.execSync(args.length ? args.map(quoteArg).join(' ') : shell, {stdio: 'inherit', shell: true})
} catch (e) {
  process.exit(typeof e.status === 'number' ? e.status : 1)
}
