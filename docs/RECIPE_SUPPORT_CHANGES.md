# Types of Recipes Supported by GG nucleus lite

For GG nucleus lite we only support basic recipe format and support for more
complex recipe will be delivered with future release. Below is the summary of
what's not supported. If it's not mentioned in the list then that case is
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
  with lite.

- Some lifecycle steps are not currently supported:

  - shutdown
  - recover
  - bootstrap (partially)

- `Skipif` section for a given lifecycle step is also not supported.

- Refering to global lifecycle requires mentioning `all` field for it to work.
  Refer to [sample recipe 3](./examples/supported_lifecyle_types/3.yaml).

- "runtime": "\*"(for classic and lite) or "runtime": "aws_nucleus_lite" is
  required new field that needs to be added for it to work with lite. See
  [sample recipe 1](./examples/supported_lifecyle_types/1.json).

  ```yaml
  Manifests:
    - Platform:
        os: "linux"
        runtime: "aws_nucleus_lite"
  ```

- Regex support is not available within recipe.

- GG nucleus lite only support variable replacement for following cases:

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
