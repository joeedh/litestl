/**
 * Binding conformance suite. run.mjs runs it once per backend (WASM via the
 * TS runtime, N-API via the native addon) with identical expectations, through
 * the TypeScript the generator emitted for tests/bindings/fixture.h.
 */
import assert from 'node:assert/strict'
import {readFileSync} from 'node:fs'
import {join} from 'node:path'
import {describe, test} from 'node:test'

import {getBindingBackend, setBindingBackend} from '@litestl/typescript-runtime'
import {Box} from 'fixture-ts/fixture/Box'
import {Color} from 'fixture-ts/fixture/Color'
import * as fx from 'fixture-ts/fixture/functions'
import type {Shape} from 'fixture-ts/fixture/Shape'
import {Vec2} from 'fixture-ts/fixture/Vec2'
import {loadBackend} from './backends'

setBindingBackend(await loadBackend())
const backend = getBindingBackend()

const vec2 = (x: number, y: number): Vec2 => backend.constructWith('fixture::Vec2', 'xy', x, y)
const close = (a: number, b: number) => assert.ok(Math.abs(a - b) < 1e-5, `${a} != ${b}`)

describe(`binding conformance (${backend.kind})`, () => {
  test('scalars', () => {
    assert.equal(fx.addInts(2, 3), 5)
    assert.equal(fx.addInts(-7, 2), -5)
    assert.equal(fx.scaleD(1.5, 4), 6)
    assert.equal(fx.isEven(4), true)
    assert.equal(fx.isEven(5), false)
    assert.equal(fx.overloaded(21), 42)
    close(fx.overloadedF(1.25), 0.625)
  })

  test('int64 round-trips as bigint', () => {
    assert.equal(fx.bigProduct(3n, 4n), 12n)
    assert.equal((fx.bigProduct as unknown as (a: number, b: number) => bigint)(3, 4), 12n, 'numbers are accepted too')
    assert.equal(fx.bigProduct(1n << 40n, 1000n), (1n << 40n) * 1000n)
  })

  test('enums', () => {
    assert.equal(fx.nextColor(Color.Red), Color.Green)
    assert.equal(fx.nextColor(Color.Blue), Color.Red)
    assert.equal(fx.colorName(Color.Blue), 'blue')
  })

  test('strings', () => {
    assert.equal(fx.greet('bob'), 'hello, bob')
    assert.equal(fx.greet('héllo ✓'), 'hello, héllo ✓')
    assert.equal(fx.textLength('abcd'), 4)
    assert.equal(fx.textLength('é'), 2, 'const char* receives UTF-8')
  })

  test('spans', () => {
    close(fx.sumFloats([1, 2, 3.5]), 6.5)
    close(fx.sumFloats(new Float32Array([0.5, 0.25])), 0.75)
    close(fx.sumFloats([]), 0)

    const plain = [0, 0, 0, 0]
    fx.fillSquares(plain)
    assert.deepEqual(plain, [0, 1, 4, 9], 'a mutable span writes back to a JS array')
    const typed = new Int32Array(3)
    fx.fillSquares(typed)
    assert.deepEqual([...typed], [0, 1, 4])
  })

  test('vectors', () => {
    assert.deepEqual([...fx.range(4)], [0, 1, 2, 3])
    const b = fx.bytes(3)
    assert.ok(b instanceof Uint8Array)
    assert.deepEqual([...b], [0, 3, 6])
    close(fx.sumVector([1, 2, 3.5]), 6.5)
  })

  test('struct construction, members and methods', () => {
    using v = vec2(3, 4)
    assert.equal(backend.typeName(v), 'fixture::Vec2')
    assert.equal(v.x, 3)
    assert.equal(v.length(), 5)
    v.y = 0
    assert.equal(v.length(), 3)

    using w = v.scaled(2)
    assert.equal(w.x, 6)
    v.add(w)
    assert.equal(v.x, 9)

    using copy = backend.constructWith('fixture::Vec2', 'copy', v) as Vec2
    copy.x = 1
    assert.equal(v.x, 9, 'the copy is independent')
  })

  test('references, pointers and by-value returns', () => {
    using a = vec2(0, 0)
    using b = vec2(2, 4)
    close(fx.vecLength(b), Math.hypot(2, 4))
    using m = fx.midpoint(a, b)
    assert.equal(m.x, 1)
    assert.equal(m.y, 2)
    fx.moveBy(a, 1, -1)
    assert.equal(a.x, 1)
    assert.equal(a.y, -1)
  })

  test('static methods', () => {
    using v = Vec2.make(5, 6)
    assert.equal(v.y, 6)
    using u = Box.unit('float')
    assert.equal(backend.typeName(u), 'fixture::Box<float>')
    assert.equal(u.extent(), 1)
  })

  test('template methods select an instantiation', () => {
    using a = vec2(1, 2)
    using b = vec2(3, 4)
    assert.equal(a.dotAs('double', b), 11)
    assert.equal(a.dotAs('float', b), 11)
  })

  test('template functions: select and infer', () => {
    const b = fx.makeBox('double', 1, 4)!
    assert.equal(backend.typeName(b), 'fixture::Box<double>')
    assert.equal(b.extent(), 3)
    assert.equal(fx.boxExtent(b), 3, 'inferred from the bound argument')
    b[Symbol.dispose]()

    const f = fx.makeBox('float', 0.5, 2)!
    assert.equal(backend.typeName(f), 'fixture::Box<float>')
    close(fx.boxExtent(f), 1.5)
    f[Symbol.dispose]()

    // Value-struct parameters (here a span) don't drive inference.
    assert.equal(fx.sumSpan('double', [1, 2.25]), 3.25)
    close(fx.sumSpan('float', new Float32Array([0.5, 0.25])), 0.75)
  })

  test('ownership', () => {
    const before = fx.liveShapes()
    const s = fx.makeShape(7)!
    assert.equal(fx.liveShapes(), before + 1)
    assert.equal(s.id, 7)
    s[Symbol.dispose]()
    assert.equal(fx.liveShapes(), before, 'disposing an owned return destroys it')

    const shared = fx.sharedShape()!
    assert.equal(shared.id, 99)
    shared[Symbol.dispose]()
    assert.equal(fx.sharedShape()!.id, 99, 'disposing a borrowed return leaves the object alone')

    assert.equal(fx.maybeShape(false), undefined)
    assert.equal(fx.maybeShape(true)!.id, 99)

    const constructed: Shape = backend.constructWith('fixture::Shape', 'main', 3, vec2(1, 1))
    assert.equal(fx.liveShapes(), before + 1)
    constructed[Symbol.dispose]()
    assert.equal(fx.liveShapes(), before)
  })

  test('embedded members: structs, arrays, pointers, strings, vectors', () => {
    using s = fx.makeShape(1)!
    assert.equal(s.weight, 1.5)
    assert.equal(s.visible, true)
    assert.equal(s.color, Color.Green)
    s.color = Color.Blue
    assert.equal(s.color, Color.Blue)

    assert.equal(s.origin.x, 1)
    s.origin.x = 10
    assert.equal(s.origin.x, 10, 'embedded structs are live views')

    assert.equal(s.corners.length, 4)
    assert.deepEqual([...s.corners], [1, 2, 3, 4])
    s.corners[2] = 30
    assert.equal(s.corners[2], 30)

    assert.equal(s.parent, undefined)

    assert.equal(s.label, '')
    s.label = 'tagged ✓'
    assert.equal(s.label, 'tagged ✓')

    const samples = s.samples
    assert.equal(samples.length, 0)
    s.addSample(1.5)
    s.addSample(2)
    assert.equal(s.countSamples(), 2)
    assert.equal(samples.length, 2, 'vector members are live views')
    assert.deepEqual([...s.samples], [1.5, 2])

    assert.equal(s.describe(), 'visible shape')
    s.visible = false
    assert.equal(s.describe(), 'hidden shape')
  })

  test('argument errors', () => {
    using v = vec2(1, 1)
    const call = (fn: (...a: any[]) => unknown, ...args: unknown[]) => () => fn(...args)
    assert.throws(call(fx.addInts, 1), TypeError, 'wrong argument count')
    assert.throws(call(fx.addInts, 'a', 1), TypeError, 'number expected')
    assert.throws(call(fx.vecLength, {}), TypeError, 'not a bound object')
    assert.throws(call(fx.boxExtent, v), TypeError, 'no instantiation for a Vec2')
    assert.throws(call(fx.moveBy, null, 1, 1), TypeError, 'non-null pointer')
    assert.throws(call(fx.makeBox, 'int32', 1, 2), TypeError, 'unknown selector')
    assert.throws(call(fx.greet, 5), TypeError, 'string expected')
    assert.throws(call(fx.sumFloats, 5), TypeError, 'array expected')
    const s = fx.makeShape(2)!
    s[Symbol.dispose]()
    assert.throws(() => s.countSamples(), 'calling into a disposed object')
  })

  test('calls do not leak', () => {
    const run = () => {
      fx.greet('x')
      fx.sumFloats([1, 2])
      fx.fillSquares([0, 0])
      fx.range(5)
      fx.bytes(5)
      fx.sumVector([1, 2])
      fx.colorName(Color.Red)
      using a = vec2(1, 2)
      using m = fx.midpoint(a, a)
      using s = fx.makeShape(5)!
      s.label = 'abc'
      s.addSample(1)
      const b = fx.makeBox('float', 0, 1)!
      b[Symbol.dispose]()
    }
    run()
    const before = backend.getMemSize()
    for (let i = 0; i < 20; i++) run()
    assert.equal(backend.getMemSize(), before)
  })

  test('the backend generates the same TypeScript as the native generator', () => {
    const files = backend.generateTypeScript()
    const gen = process.env.LSTL_FIXTURE_GEN!
    for (const name of ['fixture/functions.ts', 'fixture/Vec2.ts', 'fixture/Box.ts', 'fixture/Shape.ts']) {
      assert.equal(files[name], readFileSync(join(gen, name), 'utf8'), name)
    }
  })
})
