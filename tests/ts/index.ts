import type {VecTest} from "./test/VecTest";
import type {string} from "./litestl/util/String";
import type {float3} from "./litestl/math/float3";
import type {Foo} from "./test/Foo";
import type {float[]} from "./litestl/util/Vector";
import type {float3[]} from "./litestl/util/Vector";

export type {VecTest} from "./test/VecTest";
export type {string} from "./litestl/util/String";
export type {float3} from "./litestl/math/float3";
export type {Foo} from "./test/Foo";
export type {float[]} from "./litestl/util/Vector";
export type {float3[]} from "./litestl/util/Vector";

/** Note: Does not include templates */
export type AllBoundTypes = {
  "test::VecTest": VecTest,
  "litestl::util::Vector": float3[],
  "litestl::util::Vector": float[],
  "litestl::util::String": string,
  "test::Foo": Foo,
  "litestl::math::float3": float3,
};
