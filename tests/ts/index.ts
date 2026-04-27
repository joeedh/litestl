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

import type {VecTest} from "./test/VecTest";
import type {float3} from "./litestl/math/float3";
import type {Foo} from "./test/Foo";

export type {VecTest} from "./test/VecTest";
export type {float3} from "./litestl/math/float3";
export type {Foo} from "./test/Foo";

/** Note: Does not include templates */
export type AllBoundTypes = {
  "test::VecTest": VecTest,
  "litestl::util::Vector": float3[],
  "litestl::util::Vector": float[],
  "test::Foo": Foo,
  "litestl::math::float3": float3,
};
