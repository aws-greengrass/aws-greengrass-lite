## Sample Greengrass nucleus lite Python Generic Component

This demo generic component requests a list of AWS S3 bucket names.

## Pre-requisite

This demo has two key requirements for it to work.

### Pre-installed python3 and python3-venv.

- As a python demo it requires the python3 interpreter (if not already
  installed) as a well as a python3 virtual environment for isolated dependency
  management.

- Alternatively, you may deploy the following recipe before deploying the given
  python demo.

```yaml
---
RecipeFormatVersion: "2020-01-25"
ComponentName: sample.ggLitePython.prerequisite
ComponentVersion: 1.0.0
ComponentType: "aws.greengrass.generic"
ComponentDescription:
  This a pre-requisite component to sample.gglite.Python component
ComponentPublisher: AWS
ComponentDependencies:
Manifests:
  - Platform:
      os: linux
      runtime: "*"
    Lifecycle:
      install:
        RequiresPrivilege: true
        Script: "apt install python3-venv -y"
```

> Python3 isn't mentioned in the recipe as usually linux distributions ship with
> python3 out of the box.

### S3 bucket permissions(if using cloud deployment)

- Greengrass's connection kit ships with the least required previlage access for
  Greengrass to run which does not include S3 access permissions.

- Please refer to `Create your component in AWS IoT Greengrass (console)`
  section mentioned in
  [this aws docs](https://docs.aws.amazon.com/greengrass/v2/developerguide/upload-first-component.html).

## Deploying the component

You may deploy the componet the following ways:

- Performing a local deployment by using the `ggl-cli`.
- Performing a cloud deployment with support of S3:
  - Upload `ggLitePython.py` artifact to a S3 bucket.
  - Uncomment the `Artifacts` section of recipe.
  - Provide S3 url under `Uri` section in recipe.

## After deploying the component

The logs for to follow the deployment progress can be accessed by:

```shell
$ journalctl -xeau ggl.core.ggdeploymentd.service
```

Once successful, use the following command to check the logs and see the list of
S3 bucket names that exist in your aws account.

```shell
$ journalctl -xeau ggl.sample.ggLitePython.service
```

> If you do not see the list of names then continue pressing the up arrow key
> until you see `HELLO WORLD` text in your logs
