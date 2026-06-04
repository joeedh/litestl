import type {float3} from '../litestl/math/float3'

/** Auto-generated file */
/* eslint-disable @typescript-eslint/no-misused-new */
/* eslint-disable @typescript-eslint/no-unused-vars */

type pointer<T = any> = number
type int8 = number
type uint8 = number
type int16 = number
type uint16 = number
type int32 = number
type uint32 = number
type int64 = number
type uint64 = number
type float = number
type double = number

export interface VecTest {
  [Symbol.dispose](): void
  pos: float3[]
  f: float[]
  flag: int32
  str: string
  print(): void
  new (): VecTest
  new (arg0: int32, arg1: double, arg2: float, arg3: boolean, arg4: boolean): VecTest
  new (b: VecTest): VecTest
}
