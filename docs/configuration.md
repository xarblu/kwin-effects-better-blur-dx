# General

### Blur Strength

Like the default KWin blur setting.
Adjusts how "sharp" the blurred background is.

### Noise Strength

Like the default KWin blur setting.
Adjusts how strong the noise applied to the blurred background is.
(Used to hide banding artifacts in smooth gradients.)

### Brightness

Part of the contrast pass.
Adjusts how bright the blurred background is.

### Saturation

Part of the contrast pass.
Adjusts how saturated the blurred background is.

### Contrast

Part of the contrast pass.
Adjusts how strong the contrast of the blurred background is.

### Force Contrast Parameters

By default Better Blur DX respects when windows provide their
own contrast pass parameters (brightness, saturation, contrast).

E.g. [Plasma surfaces provide their own values](https://invent.kde.org/plasma/libplasma/-/blob/v6.5.3/src/desktoptheme/breeze/plasmarc)
which may or may not look good to you.

To force your settings to be respected for all windows enable this.

### Corner Radius

Round the corners of the blurred background by this amount.
This is mainly useful to work around the infamous "korners" bug
(square blur region leaking out behind rounded windows).

> [!NOTE]
> This setting is ignored for windows that provide their own blur region (e.g. Plasma surfaces).
> We expect these to be set correctly by the window (and usually they are).

# Force blur

### Classes of windows to force blur:

Window classes that should be force blurred (or excluded if `Blur all except matching` is enabled).
One window class per line.

> [!NOTE]
> Better Blur DX respects blur regions set by windows themselves (e.g. Plasma surfaces).
> Force blur settings are ignored in these cases.

### Blur window decorations as well

Whether to blur window decorations, including borders.
Enable this if your window decoration doesn't support blur.

This option will override the blur region specified by the decoration.

### Blur menus / Blur docks

Allows menus / docks to be blurred.
By default these are filtered from the list of blurred windows.

> [!NOTE]
> This only *allows* these to be blurred.
> You still need to add them to the force blurred window classes or enable `Blur all except matching`.
