/**
 * Runtime symbol used as the discriminator key in generated TS interfaces
 * for enum-discriminated template instantiations (see the TypeScript
 * generator's handling of `types::ParentTemplateParam::disambiguator`).
 *
 * A struct like `UniformDef<T, K extends keyof UniformBindTypeMap>` carries
 * `[getTypeSymbol]: K`, which lets TypeScript narrow other `K`-keyed fields
 * (e.g. `defaultValue: UniformBindTypeMap[K]`) once a runtime value is
 * checked against this symbol.
 */
export const getTypeSymbol = Symbol('getTypeSymbol')
