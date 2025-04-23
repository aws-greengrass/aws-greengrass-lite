# aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

{
  description = "AWS IoT Greengrass runtime for constrained devices.";
  inputs.flakelight.url = "github:nix-community/flakelight";
  outputs = { flakelight, ... }@inputs: flakelight ./. ({ lib, ... }: {
    systems = lib.systems.flakeExposed;
    inherit inputs;
    pname = "ggl";
  });
}
