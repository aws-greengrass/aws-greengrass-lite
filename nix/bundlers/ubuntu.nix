{ runCommand
, gnutar
, ...
}:
drv: runCommand "${drv.name}" { } ''
  cp -r ${drv} $out
  chmod -R u+w $out
  cd $out
  patchelf --remove-rpath lib/* plugins/* || true
  patchelf --set-rpath '$ORIGIN/../lib' bin/* || true
  patchelf --set-interpreter /lib64/ld-linux-x86-64.so.2 bin/* || true
''
