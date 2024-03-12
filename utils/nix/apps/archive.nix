{ src, gnutar, ... }:
''
  ${gnutar}/bin/tar -czf "aws-greengrass-lite.tar.gz" --posix --sort=name \
    --no-acls --no-selinux --no-xattrs --numeric-owner --owner=0 --group=0 \
    --mode='a+rwX' --mtime="1970-01-01" -C ${src} \
    --transform 's|\.|aws-greengrass-lite|' \
    -- .
''
