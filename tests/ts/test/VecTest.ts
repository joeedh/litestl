
import type {float3} from "../litestl/math/float3";

/** Auto-generated file */
/* eslint-disable @typescript-eslint/no-misused-new */
/* eslint-disable @typescript-eslint/no-unused-vars */

type float = number;
type pointer<T=any> = number;
type int = number;
type uint = number;
type double = number;
type short = number;
type ushort = number;
type char = number;
type uchar = number;
export interface VecTest {
  [Symbol.dispose](): void;
  pos: float3[]
  f: float[]
  flag: int
  str: string
  print(): void
  new(): VecTest
  new(arg0: int, arg1: double, arg2: float, arg3: boolean, arg4: boolean): VecTest
}
