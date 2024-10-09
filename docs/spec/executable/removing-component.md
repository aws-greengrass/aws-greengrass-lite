# `ggdeploymentd` spec

## Removing stale components

- Receive a Map that contains the component_name and version
- Received Map will contain the information of all the components and version
  across all thing groups
- Remove any components that does not match the exact component name and version
- Will also support deactivating services related to the component as well as
  unit files, script files, artifacts and recipe files

- Note: Currently excludes local deployments and might result to removal of all
  those components

## samples

The expected format of the input map will look as below

```GglMap
## Type of GglMap
{
    "ggl.HelloWorld": "1.0.0",
    "ggl.NewWorld": "2.1.0"
}
```
