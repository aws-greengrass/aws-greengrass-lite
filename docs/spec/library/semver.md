# semver spec

The semver library is for comparing currently available version with the
provided requirements range. The library abides by the below specification

- [semver-1] Accepts a two string parameters, `version` and
  `requirements_range`.
- [semver-2] `version` is the value you want to compare against and
  `requirement_range` for which version may fall in
- [semver-3] It will return boolean `true` if the version falls in the range and
  `false` if it doesn't
- [semver-4] `version` parameters must contain 2 `.`s to be considered valid.
- [semver-5] `requirement_range` parameters may be provided in a value range
  such as `">1.0.0 1.0.1 =1.0.2 >=1.1.1 <2.0.0"`
- [semver-6] If `requirement_range` parameters specifies a range below `0.0.0`
  it returns false
- [semver-7] Empty `requirement_range` will be treated as any version is valid
- [semver-8] Max requirement within `requirement_range` can be 512 values.
