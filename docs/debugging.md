# Debugging Better Blur DX

## Debug Logs

To enable debug logs set the following environment variable for Kwin:

```ini
QT_LOGGING_RULES=kwin_effect_better_blur_dx.*.debug=true
```

When using systemd you can do this using a drop-in for the user-unit `plasma-kwin_{wayland,x11}.service`:

```ini
# Wayland:
# ~/.config/systemd/user/plasma-kwin_wayland.service.d/bbdx_debug.conf
# X11:
# ~/.config/systemd/user/plasma-kwin_x11.service.d/bbdx_debug.conf
[Service]
Environment=QT_LOGGING_RULES=kwin_effect_better_blur_dx.*.debug=true
```

Then (after reloading the session e.g. through logout+login) debug logs should be available via `journalctl`.
To follow logged messages as they come in:

Wayland:

```
journalctl --user -fu plasma-kwin_wayland.service
```

X11:

```
journalctl --user -fu plasma-kwin_x11.service
```
