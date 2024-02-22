{ lib
, writeShellApplication
, gnutar
, src
, ...
}:
lib.getExe (writeShellApplication {
  name = "create_archive";
  runtimeInputs = [ gnutar ];
  text = ''
    tar -czf "aws-greengrass-lite.tar.gz" --posix --sort=name --no-acls \
      --no-selinux --no-xattrs --numeric-owner --owner=0 --group=0 \
      --mode='a+rwX' --mtime="1970-01-01" -C ${src} \
      --transform 's|\.|aws-greengrass-lite|' \
      -- .
  '';
})
