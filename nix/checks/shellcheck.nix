pkgs: ''
  ${pkgs.shellcheck}/bin/shellcheck $(${pkgs.fd}/bin/fd -e sh -e bash)
''
