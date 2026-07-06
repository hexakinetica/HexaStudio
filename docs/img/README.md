# Screenshots

Rendered UI reference images for the HexaStudio README live here.

Generate them reproducibly from the module benches (offline, deterministic) or from a live probe:

```bash
build/bin/app_shell_bench.exe --screenshot docs/img/shell.png          # assembled HMI
build/bin/jog_control_bench.exe   --screenshot docs/img/jog.png
build/bin/status_bar_bench.exe    --screenshot docs/img/status.png
build/bin/hal_control_bench.exe   --screenshot docs/img/hal.png
build/bin/overlays_bench.exe      --screenshot docs/img/settings.png
build/bin/HexaStudioNG.exe --probe 5 --screenshot docs/img/hexastudio_live.png
```

Then embed the PNGs in [`../../README.md`](../../README.md).
