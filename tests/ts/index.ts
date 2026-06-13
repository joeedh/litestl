/** Auto-generated file */
/* eslint-disable @typescript-eslint/no-misused-new */
/* eslint-disable @typescript-eslint/no-unused-vars */

type pointer<T=any> = number;
type int8 = number;
type uint8 = number;
type int16 = number;
type uint16 = number;
type int32 = number;
type uint32 = number;
type int64 = number;
type uint64 = number;
type float = number;
type double = number;


import type {VecTest} from "./test/VecTest";
import type {float3} from "./litestl/math/float3";
import type {Foo} from "./test/Foo";

export type {VecTest} from "./test/VecTest";
export type {float3} from "./litestl/math/float3";
export type {Foo} from "./test/Foo";

/** Note: Does not include templates */
export type AllBoundTypes = {
  "test::VecTest": VecTest,
  "test::Foo": Foo,
  "litestl::math::float3": float3,
};
