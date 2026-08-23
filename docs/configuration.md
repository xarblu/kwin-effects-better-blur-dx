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

Blends the blurred background toward **Mica Tint** using a luminosity blend,
reproducing the Windows 11 Mica material's colouring.

At `0%` (the default) the blend is skipped entirely and the effect behaves as it
always has. At higher values the background keeps the *lightness* of what is
behind the window while taking its *hue and saturation* from the tint colour.

The practical result is that light and dark structure from the wallpaper still
shows through, but colour bleed does not — which keeps text contrast predictable
regardless of what the wallpaper looks like.

> [!NOTE]
> This is a genuine luminosity blend (the `SetLum`/`ClipColor` operation from the
> W3C compositing spec), not a colour matrix. It cannot be reproduced with the
> Brightness, Saturation, and Contrast sliders, which are matrix operations.

### Mica Tint

The colour that **Mica Strength** blends toward. Only its hue and saturation are
used; its lightness is supplied by the blurred background.

Match it to the background colour your applications use so windows and their
surroundings read as one material. Has no effect while Mica Strength is `0%`.

> [!TIP]
> For the closest match to the Windows 11 Mica material, pair this with
> `Blur Source Mode: Wallpaper` on the Performance tab. Mica samples only the
> desktop wallpaper, never the windows underneath, and wallpaper mode is what
> makes the blur behave the same way here.

The blend is applied in both the contrast pass and the refraction pass, so it
works regardless of whether refraction is enabled.

### Mica Luminosity

How much of the backdrop's *lightness* reaches the blended result.

At `100%` (the default) the surface takes its lightness entirely from whatever is
behind the window, so a dark region of wallpaper produces a dark surface even
when the tint is nearly white. Lower values pull the lightness toward the tint's
own, so the surface varies *around* the tint rather than ranging freely.

This is the equivalent of Fluent's *luminosity opacity*, and it is what keeps the
real Mica material reliably light in light themes and reliably dark in dark ones.
Values around `40-50%` preserve visible wallpaper structure while keeping the
surface close enough to the tint that normal text colours stay legible on it.

> [!NOTE]
> Only relevant while **Mica Strength** is above `0%`. Leaving this at `100%`
> reproduces the behaviour of earlier versions exactly.

### Follow Colour Scheme

Switch between a light and a dark tint automatically, following the active
colour scheme. Useful with Plasma's automatic day/night appearance switching.

The scheme's `Colors:Window` background decides which tint applies, using the
same luma weights as the shader. Changes are picked up live -- no reconfigure or
restart is needed when the scheme switches.

### Mica Tint (Dark)

The tint used while **Follow Colour Scheme** is enabled and the active colour
scheme is dark. **Mica Tint** becomes the light-scheme tint in that case, and is
relabelled accordingly.

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
