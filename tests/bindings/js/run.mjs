// Binding conformance runner: type-checks the generated fixture TS against the
// runtime, bundles the suite, and runs it once per backend.
//
//   node run.mjs (--gen <generated TS dir> | --gen-exe <binding_fixture_gen_ts>)
//                [--wasm <fixture .wasm>] [--napi <fixture .node>] [--out <scratch dir>]
//
// Every backend runs the same suite with the same expectations.
import {spawnSync} from 'node:child_process'
import {existsSync, mkdirSync, writeFileSync} from 'node:fs'
import {dirname, join, resolve} from 'node:path'
import {fileURLToPath} from 'node:url'
import {parseArgs} from 'node:util'

const here = dirname(fileURLToPath(import.meta.url))
const runtime = resolve(here, '../../../binding/typescriptRuntime')

const {values: opts} = parseArgs({
  options: {
    gen: {type: 'string'},
    'gen-exe': {type: 'string'},
    wasm: {type: 'string'},
    napi: {type: 'string'},
    out: {type: 'string'},
  },
})
if ((!opts.gen && !opts['gen-exe']) || (!opts.wasm && !opts.napi)) {
  console.error('usage: node run.mjs (--gen <dir> | --gen-exe <exe>) [--wasm <file>] [--napi <file>] [--out <dir>]')
  process.exit(2)
}
const out = resolve(opts.out ?? 'binding-conformance')
const gen = opts.gen ? resolve(opts.gen) : join(out, 'fixture-ts')
mkdirSync(out, {recursive: true})

const bin = (name) => join(here, 'node_modules', '.bin', process.platform === 'win32' ? `${name}.cmd` : name)
const run = (cmd, args, env = {}) => {
  const options = {stdio: 'inherit', env: {...process.env, ...env}}
  // Windows runs .cmd shims only through a shell, which needs the quoting.
  const r = cmd.endsWith('.cmd')
    ? spawnSync([cmd, ...args].map((a) => `"${a}"`).join(' '), {...options, shell: true})
    : spawnSync(cmd, args, options)
  return r.status ?? 1
}
if (!existsSync(bin('tsc'))) {
  console.error(`missing dev dependencies: run "npm ci" in ${here}`)
  process.exit(2)
}

const paths = {
  '@litestl/typescript-runtime': [join(runtime, 'index.ts')],
  'fixture-ts/*': [join(gen, '*')],
}

const tsconfig = join(out, 'tsconfig.json')
writeFileSync(
  tsconfig,
  JSON.stringify(
    {
      compilerOptions: {
        target: 'ES2022',
        module: 'ESNext',
        moduleResolution: 'bundler',
        strict: true,
        noEmit: true,
        skipLibCheck: true,
        isolatedModules: true,
        lib: ['ES2022', 'DOM', 'ESNext.Disposable'],
        types: ['node'],
        typeRoots: [join(here, 'node_modules', '@types')],
        paths,
      },
      files: [join(here, 'conformance.test.ts')],
      include: [join(gen, '**', '*.ts')],
    },
    null,
    2
  )
)

if (opts['gen-exe']) {
  console.log('== generating fixture TS')
  if (run(resolve(opts['gen-exe']), [gen]) !== 0) {
    process.exit(1)
  }
}

console.log('== tsc: generated fixture TS + runtime')
if (run(bin('tsc'), ['-p', tsconfig]) !== 0) {
  process.exit(1)
}

const bundle = join(out, 'conformance.test.mjs')
console.log('== esbuild')
const status = run(bin('esbuild'), [
  join(here, 'conformance.test.ts'),
  '--bundle',
  '--platform=node',
  '--format=esm',
  '--target=node20',
  `--alias:@litestl/typescript-runtime=${join(runtime, 'index.ts')}`,
  `--alias:fixture-ts=${gen}`,
  `--outfile=${bundle}`,
  '--log-level=warning',
])
if (status !== 0) {
  process.exit(1)
}

let failed = false
for (const [kind, file] of [
  ['wasm', opts.wasm],
  ['napi', opts.napi],
]) {
  if (!file) continue
  console.log(`== ${kind}: ${file}`)
  const env = {LSTL_BACKEND: kind, LSTL_FIXTURE: resolve(file), LSTL_FIXTURE_GEN: gen}
  if (run(process.execPath, ['--test', '--test-reporter=spec', bundle], env) !== 0) {
    failed = true
  }
}
process.exit(failed ? 1 : 0)
