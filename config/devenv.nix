{
  lib,
  pkgs,
  config,
  inputs,
  ...
}:

{
  imports = [
    ./env.nix
    ./packages.nix

    ./languages
  ];

  # processes.dev.exec = "${lib.getExe pkgs.watchexec} -n -- ls -la";

  # services.postgres.enable = true;

  scripts.hello.exec = ''
    echo hello from $GREET
  '';

  enterShell = ''
    hello         # Run scripts directly
    git --version # Use packages
  '';

  # tasks = {
  #   "myproj:setup".exec = "mytool build";
  #   "devenv:enterShell".after = [ "myproj:setup" ];
  # };

  enterTest = ''
    echo "Running tests"
    git --version | grep --color=auto "${pkgs.git.version}"
  '';

  # git-hooks.hooks.shellcheck.enable = true;
}
