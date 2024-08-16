pkgs: ''
  ${pkgs.nodePackages.cspell}/bin/cspell "**" --quiet
  ${pkgs.coreutils}/bin/sort -cuf misc/dictionary.txt
''
