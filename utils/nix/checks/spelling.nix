pkgs: ''
  ${pkgs.nodePackages.cspell}/bin/cspell "**" --quiet
  ${pkgs.coreutils}/bin/sort -cuf utils/dictionary.txt
''
