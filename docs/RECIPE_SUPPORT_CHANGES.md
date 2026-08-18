# Types of Recipes Supported by Greengrass nucleus lite

For Greengrass nucleus lite we only support basic recipe format and support for
more complex recipe will be delivered with future release. Below is the summary
of what's not supported. If it's not mentioned in the list then that case is
supported as mentioned in the
[aws docs](https://docs.aws.amazon.com/greengrass/v2/developerguide/component-recipe-reference.html).

## Major differences

### Recipes

- All the keys in a recipe are now case sensitive, please visit our aws recipe
  docs reference
  [link](https://docs.aws.amazon.com/greengrass/v2/developerguide/component-recipe-reference.html)
  to know aboout the correct casing.

- Only linux lifecycles are supported with the current release.

- Only generic component (`aws.greengrass.generic`) recipe types are supported
  with Greengrass nucleus lite.

- Some lifecycle steps are not currently supported:

  - shutdown
  - recover
  - bootstrap (partially)

- `Skipif` section for a given lifecycle step is also not supported.

- Refering to global lifecycle requires mentioning `all` field for it to work.
  Refer to [sample recipe 3](./examples/supported_lifecyle_types/3.yaml).

- "runtime": "\*"(for Greengrass V2) or "runtime": "aws_nucleus_lite" is
  required new field that needs to be added for it to work with Greengrass
  nucleus lite. See
  [sample recipe 1](./examples/supported_lifecyle_types/1.json).

  ```yaml
  Manifests:
    - Platform:
        os: "linux"
        runtime: "aws_nucleus_lite"
  ```

- Regex support for platform attribute matching is available using a built-in
  allocation-free Thompson NFA regex engine. Platform attribute values (os,
  runtime, architecture) enclosed in forward slashes (e.g. `/linux|windows/`)
  are evaluated as regex patterns against a whole-string match. The literal
  wildcard `*` continues to match any value, and undelimited values use exact
  comparison.

  **Engine design:** The regex engine is a linear-time Thompson NFA simulation
  with no dynamic memory allocation and no backtracking. This makes catastrophic
  backtracking structurally impossible and satisfies the codebase
  zero-allocation constraint. All state is kept in fixed-size stack arrays.

  **Supported regex syntax subset:**

  - Literal characters
  - `.` (match any single byte)
  - Quantifiers: `*`, `+`, `?`
  - Alternation: `|`
  - Grouping: `(...)` (no capture semantics)
  - Bracket expressions: `[abc]`, `[a-z]`, `[^0-9]` (ranges and negation)
  - Backslash escaping of metacharacters: `\.`, `\(`, etc.
  - Redundant anchors `^` (at start) and `$` (at end) are accepted as no-ops

  **NOT supported (vs Java regex / PCRE):**

  - `\d`, `\w`, `\s` and their negations are **rejected** (return parse error).
    Use `[0-9]`, `[a-zA-Z0-9_]`, or a literal space instead. (Platform attribute
    values never contain tabs or newlines, so a single space suffices for `\s`.)
  - `{` and `{n,m}` counted quantifiers are **rejected** (not matched literally)
  - Backslash escapes inside bracket expressions are **rejected** — there is no
    escape processing inside `[...]`; use POSIX literal rules instead
  - Lookahead and lookbehind assertions (`(?=...)`, `(?<=...)`)
  - Non-greedy quantifiers (`*?`, `+?`)
  - Named capture groups (`(?<name>...)`)
  - Unicode property escapes (`\p{...}`)
  - Backreferences

  **Fail-closed behavior:** If a pattern exceeds 256 bytes, exceeds 512 NFA
  instructions, nests parentheses deeper than 16 levels, or contains a syntax
  error, the match returns false and a warning is logged. A malformed pattern
  will never cause a manifest to be selected.

  In practice, platform attribute patterns in published recipes use only simple
  alternation (e.g. `/aarch64|x86_64/`) and character classes, which work
  identically in both this engine and the Java nucleus.

- Greengrass nucleus lite only support variable replacement for following cases:

  - artifacts:path
  - artifacts:decompressedPath
  - kernel:rootPath
  - iot:thingName
  - work:path
  - Limited configuration:json_pointer support

- Component_dependency_name prefixes are not supported for recipe variable
  replacement.
- Recipe variable interpolation for component configuration is not supported.

### Nucleus Configuration

- Platform Override only supports `architecture.detail`, please refer known
  issues link
  [here](https://github.com/aws-greengrass/aws-greengrass-lite/issues).
- When `architecture.detail` is not set in `platformOverride`, the deployment
  service will automatically detect it at compile time on ARM platforms. The
  following values are reported based on the target architecture:
  - `armv6l` / `armv6b` (ARMv6 little/big endian)
  - `armv7l` / `armv7b` (ARMv7 little/big endian)
  - `armv8l` / `armv8b` (ARMv8 little/big endian)
  - On non-ARM platforms or unrecognized ARM versions, `architecture.detail` is
    omitted from the component resolution request.
