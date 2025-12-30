# DEV
Things not in any tagged release yet:

### Features:
- Match window classes with regular expressions
- Explicitly force-blurred windows are now always blurred - even when transforming. (May or may not improve compatibility with things like Wobbly Windows. Instead of blur disappearing and potential flickering we now have a somewhat off-looking blurred square behind the "wobble area".)

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
