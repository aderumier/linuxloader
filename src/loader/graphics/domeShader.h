#pragma once

// Flattening of a dome-screen game's (Gundam: Bonds of the Battlefield's)
// frame: the cabinet's screen was a hemisphere lit by a DLP projector, and
// the game draws its scene with the wide angular projection that looks
// right on the dome. On a flat display its raw dome projection (the "dome
// effect") remains. Right before the game's swap, its frame (the window's
// buffer it drew to) is copied into a texture and drawn back through a
// shader computing the inverse of the dome's projection: a flat
// perspective view (the config's DOME_* choose it).
//
// Called with the game's context current (from namcoEs1BeforeSwap), width x
// height the window's size: returns 1 when the pass was done, 0 when it was
// not (DOME_CORRECTION off, the GL cannot).

int domeShaderPresent(int width, int height);
