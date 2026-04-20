## Typescript runtime for the litestl binding system
* Read the files to understand them.

## INeededWasm.HEAP** members
There are various helper typed array properties on INeededWasm to deal with binary data:
* HEAPU8: unsigned bytes
* HEAPU16: unsigned 2-byte shorts
* HEAPU32: unsigned 4-byte ints
* HEAPU8: signed bytes
* HEAPU16: signed 2-byte shorts
* HEAPU32: signed 4-byte ints
* HEAPF32: 4-byte floats
* HEAPF64: 8-byte doubles
* HEAPPTR: 4-byte pointers

In addition there are a number of helper constants:
* INT32SHIFT : how much to bitwise shift left to get a valid index in HEAP32 or HEAPU32
* INT16SHIFT : how much to bitwise shift left to get a valid index in HEAP16 or HEAPU16
* PTRSHIFT : how much to bitwise shift left to get a valid index in HEAPPTR

For example:
```ts

function dereferenceIntPtr(wasm: INeededWasm, ptr: number) {
	return wasm.HEAP32[wasm.HEAPPTR[ptr >> wasm.PTRSHIFT] >> wasm.INT32SHIFT];
}

```