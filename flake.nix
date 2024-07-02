# aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

{
  description = "AWS IoT Greengrass runtime for constrained devices.";
  inputs.flakelight.url = "github:accelbread/flakelight";
  outputs = { flakelight, ... }: flakelight ./. ({ lib, ... }: {
    systems = lib.systems.flakeExposed;
    pname = "ggl";
  });
}
