{ inputs
, outputs
, system
, runCommand
}:
(import inputs.nixpkgs {
  inherit system;
  crossSystem.config = "aarch64-unknown-linux-gnu";
  overlays = [ outputs.overlays.default ];
}).sample-component
