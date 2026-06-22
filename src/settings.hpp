#pragma once

/**
 * File containing setting enums
 */

namespace BBDX {

/**
 * Choose what to blit i.e.
 * what should be the "base texture"
 * for the blur
 */
enum BlitMode {
    // blit the RenderTarget i.e.
    // what's actually on screen at
    // any given moment
    RENDER_TARGET,

    // blit only the wallpaper
    // allowing for aggressive cache usage
    WALLPAPER,
};

}
