{
  pkgs,
  config,
  ...
}:
{
  packages =
    with pkgs;
    [
      platformio
      mkspiffs-presets.arduino-esp32

      pulumi-esc

      # espup install # . $HOME/export-esp.sh
      # espup
      ldproxy
      esptool
      espflash
      esp-generate
      cargo-espmonitor
    ]
    ++ lib.optionals config.languages.c.enable [
      ninja
      ccache
      openocd
      binsider # binary inspector TUI
      dfu-util
      probe-rs-tools # rust-based replacement for stmcli, openocd, etc.
    ];
}
