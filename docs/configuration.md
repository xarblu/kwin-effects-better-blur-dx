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

### Mica Strength

Blends the blurred background in a Mica effect by using a luminosity blend.

At `0%` (the default) no blend is used. 
At higher values the background keeps the luminosity of what is
behind the window while taking the hue and saturation from the tint color.

### Mica Tint

The color that Mica Strength blends towards. Only its hue and saturation are
used, its luminosity is supplied by the blurred background.

Has no effect while Mica strength is 0%

Pair this with`Blur Source Mode: Wallpaper` on the Performance tab for
a more faithful effect.

### Mica Luminosity

How much of the backdrop's 'lightness' reaches the blended result.

At '100%' (the default) the surface takes its lightness entirely from whatever is
behind the window, so a dark region of wallpaper produces a dark surface even
when the tint is nearly white. Lower values instead lean more towards the tint's
own.

This keeps the material reliably dark and light regardless of what is behind it. 

### Follow Color Scheme

Switch between a light and a dark tint automatically following the system's
setting. 

The scheme's `Colors:Window` background decides which tint applies, using the
same luma weights as the shader.

### Mica Tint (Dark)

The tint used while 'Follow Color Scheme' is enabled and the active color
scheme is dark.

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
