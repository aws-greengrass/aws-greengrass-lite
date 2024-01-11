{ src
, outputs'
, gglite-cross-aarch64
, sample-component-cross-aarch64
, runCommand
, gnutar
}:
let
  pkg-bin = outputs'.bundlers.ubuntu gglite-cross-aarch64;
  sample-component-bin = outputs'.bundlers.ubuntu sample-component-cross-aarch64;
in
runCommand "gglite-bundle.tar.gz" { } ''
  mkdir gglite-bundle
  cp -r ${src} gglite-bundle/aws-greengrass-lite
  cp -r ${pkg-bin} gglite-bundle/aws-greengrass-lite-bin
  cp -r ${sample-component-bin} gglite-bundle/sample-component-bin
  ${gnutar}/bin/tar -czf $out --posix --sort=name --numeric-owner --owner=0 \
    --group=0 --mode='a+rwX' --mtime="1970-01-01" -- gglite-bundle
''
