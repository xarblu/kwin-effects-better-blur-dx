# DEV
Things not in any tagged release yet:

### Features
- **Plasma 6.7 support**
- **(Almost) Full BlurCache rewrite** (I lost count but I think that's like the 5th rewrite :P)  
  - The cache should now be more performant and reliable
    compared to the previous iterations (especially around partial repaints) which would
    previously lead to a lot of unneeded cache invalidation.
  - Queries are now async, meaning the GPU should be able to pipeline them better
    (because we don't force it to complete all work up to the query just to get the result).
    The next time KWin paints the scene we check the queries for completion and update
    the blur where needed.
- Blur of fully covered windows now gets frozen.
  The idea is that this should avoid unnecessary query spam caused by windows above
  while not really looking worse.  
  (This is a dirty hack and likely won't solve that stacked windows are just stupidly inefficient.
  Example (oversimplified): You have 5 blurred windows stacked. The *top window* is playing a video at 60FPS.
  How many blur repaints is that per second? 60 because only the top window needs repaints, right?
  Well it's actually `5 * 60 = 300` because every video frame causes the entire stack to be repainted from bottom to top.
  The more windows you stack the worse the situation gets.)

### Internal
- Tweaked the texture compare query + shaders to be faster
- Don't invalidate cache on opacity change (this was left over from
  when opacity was still part of the cached texture)
- There is now a unified `kwin_compat.hpp` header which automatically
  includes `kwin_version.hpp` and all appropriate `kwin_compat_<version>.hpp` headers
  depending on the build environment.

# 2.4.1

Version bump to fix version string - no actual changes

# 2.4.0

### Features:
- **Blur Caching** to (hopefully) improve performance of the effect.
  This is my 3rd attempt at "making the effect less GPU hungry"
  and changes a good chunk of the rendering pipeline.
  At least on my machine and testing VM it does look quite promising
  and I haven't seen any issues reported since the introduction in `main`
- **Higher Noise Strength Limits** in case you wanted to increase those more without
  digging through `kwinrc`. The limit was increased from 14 to 50.
- **Spinboxes for Blur Strength and Noise Strength** if you prefer numbers over sliders - and to be consistent with the other options.

### Bug Fixes:
- Fixed `maximizedState` detection with "slim" panels that don't cover a full screen edge
- Fixed incomplete repaints e.g. when a video is playing behind a blurred surface (needs KWin 6.6.4+)

### Internal
- Cleaned up a bunch of dead code and shaders
- Assume kwin-x11 has API version 6.5.0 (it hasn't really seen any updates since then)
- `WindowManager` now talks directly to the `BlurEffect` without going through the Qt slot loop

# 2.3.0

### Features:
- New rounded corners pass to stop rectangular noise texture from "leaking"
- Debug logging for (toggled with `QT_LOGGING_RULES=kwin_effect_better_blur_dx.*.debug=true`):
  - Added windows
  - Removed windows
  - `blurOrigin` changes
  - `maximizedState` changes

### Bug Fixes:
- More pedantic `isPlasmaSurface` check. We now need to be *very sure* something
  is a special Plasma surface (to the degree of matching window classes) and else
  assume it isn't; should be the better trade-off because most bugs I keep seeing
  boil down to "this is a false-positive in `isPlasmaSurface`" and only rarely
  "this should be treated as a Plasma surface"
- Only take "opacity affects blur" path if opacity changed at during a window's lifetime.
- Explicitly calculate `maximizedState` once on `BBDX::Window` creation and
  track our own `isFullscreen`/`isMinimized`. Fixes rounding of windows that spawn
  fullscreen/maximized and don't change their geometry after (like the Plasma logout greeter).

### Internal:
- Most `shouldForceBlur` logic is now attached to `BBDX::Window` (instead of `BBDX::WindowManager`)
  which saves some `std::unordered_map` lookups.

# 2.2.0

### Features:
- Port to Plasma 6.6 blur
- Support Plasma 6.6
- Re-implement refraction as an alternative to the default contrast pass

### Bug Fixes:
- Fixed falsely overwriting blur regions with empty force-blur regions resulting e.g. in decorations losing their blur.

# 2.1.0

### Features:
- Match window classes with regular expressions
- Improved "blur while transformed" behaviour - and thus compatibility with Wobbly Windows.  
  Before the behaviour of BBDX in conjuction with Wobbly Windows was somewhat "undefined" (blur randomly disappearing, flickering, blur region "leaking" behind the window, ...).  
  Now we explicitly disable blur on window move/resize, let it transform like it wants, then explicitly reapply our blur.  
  It's not perfect but it's consistent and predictable. (And there even is a very cool fade when transitioning between blurred ↔ non-blurred states 🚀)

### Bug Fixes:
- Fix not respecting requested blur by X11 clients on the Wayland build. The X11 property parsing logic was accidentally made exclusive to the X11 build.
- Fix corner radius with "Round bottom corners of windows with no borders" (Breeze decoration settings) and "Blur decorations as well" both enabled.

### Internal:
- Introduce `WindowManager` and `Window` classes to track all relevant window info, perform matching, etc.

# 2.0.0
The first release of Better Blur DX 🎉

> [!IMPORTANT]
> If coming from the original Better Blur: This release contains **breaking changes**.

### Features:
- Port to the Plasma 6.5 blur
- ***BREAKING:*** Changed config group name - If coming from the original Better Blur you'll have to reconfigure
- ***BREAKING:*** Removed refraction - This needs a major rewrite in order to work with the new contrast pass of the Plasma 6.5 blur.
  If there is a lot of interest it might come back some day but for now it's gone to ease maintenance.
- ***BREAKING:*** Removed static blur - This used a bunch of hacks and IMHO isn't worth the maintenance effort.
  It essentially duplicated the *entire effect pipeline* for IMO very little gain.
- Contrast pass parameters can be configured via sliders or spinboxes
- Allow forcing contrast pass parameters - E.g. the Plasma panel provides its own values which we respect by default.
  This allows users to override those values if the window-provided values look "off"
- Auto detect "window opacity affects blur" - The option was removed and we now detect if a window's blur should be
  affected by its opacity or not based on some heuristics.

### Bug fixes:
- Fix blurring of windows with opacity < 1.0 (from KWin rules)
- Fix artifacts around screen edges in the new contrast pass

### Misc:
- Rename to "Better Blur DX"

# <= 1.5.0
Older version are from the original Better Blur - we just kept the version going from here.
