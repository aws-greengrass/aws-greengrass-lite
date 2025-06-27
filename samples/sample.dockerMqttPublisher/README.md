## Sample Greengrass nucleus lite Python in Docker component

This demo generic component publishes to IoT Core from within a Docker container

## Pre-requisite

This demo has two key requirements for it to work.

### Pre-installed docker or podman (Host system)

- As a container demo, the image must be built and uploaded in order for
  Greengrass to download and run
- Build the container using the following command (docker or podman):

```shell
docker build samples/sample.dockerMqttPublisher/artifacts -t publish_to_iot_core
```

You may deploy the componet the following ways:

- Performing a local deployment by using the `ggl-cli` (remove install step;
  image must be available locally to run)
- Performing a cloud deployment with support of S3:
  - Archive the container image:

```shell
docker image save publish_to_iot_core | gzip > publish_to_iot_core.tar.gz
```

- Upload `publish_to_iot_core.tar.gz` artifact to a S3 bucket.
- Uncomment the `Artifacts` section of recipe.
- Provide S3 url under `Uri` section in recipe.
- Performing a cloud deployment with support of ECR:

```shell
docker tag publish_to_iot_core 01234567890.dkr.ecr.REGION-CODE.amazonaws.com/REGISTRY/publish_to_iot_core:latest
docker push 01234567890.dkr.ecr.REGION-CODE.amazonaws.com/REGISTRY/publish_to_iot_core:latest
```

- Modify installation step to instead re-tag the downloaded image to
  publish_to_iot_core
- Provide docker URL under `Uri` section in recipe (e.g.
  `docker:01234567890.dkr.ecr.REGION-CODE.amazonaws.com/REGISTRY/publish_to_iot_core:latest`)

## After deploying the component

The logs for to follow the deployment progress can be accessed by:

```shell
$ journalctl -xeau ggl.core.ggdeploymentd.service
```

Once successful, use the following command to check the logs and see the list of
S3 bucket names that exist in your aws account.

```shell
$ journalctl -xeau ggl.sample.dockerMqttPublisher.service
```

> If you do not see the list of names then continue pressing the up arrow key
> until you see `Preparing to publish HELLO_WORLD to my/topic with QoS=1` text
> in your logs
