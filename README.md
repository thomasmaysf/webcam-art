# Pixel Mirror — webcam art

An interactive [p5.js](https://p5js.org/) sketch that rebuilds your webcam feed out of code, built to **demystify creative coding for a live demo audience**. Every visual is driven by a handful of named variables, and a live code panel highlights the exact line that changes as you move a control — so the group *sees* the link between a number in code and a change on screen.

**[▶ Live demo](https://thomasmaysf.github.io/webcam-art/)** (once GitHub Pages is enabled — see below)

## Files

| File | What it is |
|------|------------|
| `index.html` | The main sketch: sliders + keyboard control + live code panel + mic reactivity |
| `editable.html` | A variant where the audience **types code directly** into an editor and it runs live |

Everything is self-contained — a single HTML file each, loading p5.js from a CDN. No build step.

## What the audience controls

| Variable | Does |
|----------|------|
| `gridSize` | Size of each chunk (resolution of the effect) |
| `threshold` | Brightness cutoff — what's "bright enough" to draw |
| `hueShift` | Rotates the colors |
| `wobble` | Amount of sine-wave warp |
| `wobAngle` | Direction of the warp (0° = horizontal, 90° = vertical) |
| `mirror` | Flip the image like a mirror |
| `invert` | Negative-film look |

Four visual **modes**: ascii dots · color blocks · edge glow · mirror kaleido.

## Keyboard controls

Click the page once so it listens for keys.

| Key | Action |
|-----|--------|
| `1` `2` `3` `4` | Switch visual mode |
| `←` `→` | `gridSize` down / up |
| `↓` `↑` | `threshold` down / up |
| `H` | `hueShift` +60° |
| `W` | `wobble` +8 |
| `A` | `wobAngle` +45° |
| `M` | Toggle mirror |
| `I` | Toggle invert |
| `R` | Reset everything |
| `space` | Start the camera |

## Microphone reactivity

Click **enable microphone**, then arm the 🎤 button next to any variable(s). The live mic volume is added *on top of* each armed variable's slider value, so loud sounds push the effect while you can still drag the slider. Disarm to release it back to the slider. Great for a "data → variable → art" moment: arm `wobble` and `gridSize` and the whole image pulses when the room gets loud.

## Running it

Camera and microphone require a **secure context**:

- **Opening the file directly** (double-click) works in most browsers.
- **`localhost`** works.
- Plain `http://` over a network will block camera/mic — use HTTPS.

### Enabling GitHub Pages (for the hosted link)

1. Repo **Settings → Pages**
2. Source: **Deploy from a branch**, branch **`main`**, folder **`/ (root)`**
3. Save. The demo will be at `https://thomasmaysf.github.io/webcam-art/`

> **Note:** `editable.html` uses `new Function()` to run audience-typed code. That's fine for a trusted classroom/demo, but means anyone typing in it can run arbitrary JavaScript — keep that page for in-person use rather than a public link.

## Privacy

All camera and microphone processing happens locally in the browser. Nothing is uploaded, recorded, or sent anywhere.

## Built with

[p5.js](https://p5js.org/) 1.9.4 and [p5.sound](https://p5js.org/reference/#/libraries/p5.sound) — no other dependencies.
