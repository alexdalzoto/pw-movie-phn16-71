# Movie sound profile for the Acer Predator PHN16-71 on Linux (PipeWire)

Makes the internal speakers of the **Acer Predator PHN16-71** (Realtek ALC245, codec SUBSYS `1025170F`)
sound like they do on Windows with the "Movie" sound profile on: same tonal balance, wider stereo image,
clearer dialog, extra bass and the same loudness.

It is **not** the original Windows software. It is a black-box reproduction: every stage was fitted to
recordings of the Windows output (impulse responses for the filters, small C plugins for the loudness
control and bass enhancement). No proprietary code or binaries are included.

## Requirements
- The laptop above. The filters were measured on it only; on any other model the result will be wrong.
- PipeWire >= 0.3.70 with WirePlumber and the `pipewire-pulse` service (the install script restarts all three).
- `gcc`, `make`, and the LADSPA headers (`ladspa.h`). The script installs the headers with pacman, apt or dnf (asks for sudo).

## Install
```
bash install.sh
```
It builds three plugins into `~/.ladspa`, copies the IRs to `~/.config/pipewire/dts-ir`, writes
`~/.config/pipewire/pipewire.conf.d/dts-movie.conf`, restarts PipeWire, makes **"DTS Movie (speakers)"**
the default output at 20%, and turns EasyEffects off if it was running. Everything it replaces is saved in
`~/dts-movie-backup-<date>`.

## Use
- Use the volume slider / Fn keys of the **"DTS Movie (speakers)"** device. The volume is applied after
  the loudness stage and follows the Windows taper (20% here sounds like 20% on Windows).
- Keep the real **Speaker** device at 100%.
- Apps must play into the DTS Movie device (it is set as default; if an app goes straight to the speakers,
  move it with `pavucontrol` or the sound settings).
- It is a loudness maximizer like the original, so quiet material is boosted a lot.
- If the bass sounds fuzzy, lower the "Amount" control of the `dts_bass` plugin in
  `~/.config/pipewire/pipewire.conf.d/dts-movie.conf`, then `systemctl --user restart pipewire`.

## Uninstall
```
bash uninstall.sh ~/dts-movie-backup-<date>
```

## Troubleshooting
- Chain not loading: `journalctl --user -u pipewire -n 50`.
- Very quiet or silent: check that the Speaker device is at 100% and not muted (`wpctl status`).
- Plugin path errors: the plugins must be in `~/.ladspa`.

## Not reproduced
Adaptive dialog enhancement on real speech and the loudness-compensation stage. The IRs were measured at
48 kHz (PipeWire resamples them at other rates). The AGC ramps up slightly faster than Windows after a
sudden jump to low levels.

## License / notes
The project is MIT-licensed (see `LICENSE`). The C sources (`plugin/`) are original code. The IRs are measurements of the Windows output of this one
laptop model. This is an independent project, not affiliated with or endorsed by DTS, Acer or Realtek;
those names belong to their owners.
