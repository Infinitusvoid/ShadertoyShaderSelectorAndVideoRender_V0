# Shadertoy Shader Selector And Video Render V0

A native desktop C++ tool for triaging a local folder of Shadertoy-style fragment shaders, previewing them, selecting the good ones, and rendering deterministic offline videos with `ffmpeg`.

This project is intentionally focused on a practical local workflow:

- scan `shaders_input`
- sanitize common Shadertoy-to-GLSL issues
- keep only working shaders in `shaders_valid`
- browse them in preview or grid mode
- queue selected shaders for offline rendering
- render image sequences offscreen
- encode final videos with `ffmpeg`

It is not a full Shadertoy clone. V0 supports single-pass fragment shaders only.

## Highlights

- Native GLFW + OpenGL desktop app
- Rule-based shader sanitizer for common Shadertoy issues
- Validation by actual compile + link + render pass
- Cached thumbnail grid for fast browsing
- Persistent render selection mirrored to disk
- Deterministic offline rendering
- `ffmpeg` integration for final video encoding
- Plain-text logs and render manifest for crash recovery

## Current Support

Supported shader styles:

- `void mainImage(out vec4 fragColor, in vec2 fragCoord)`
- regular fragment shaders with `main()`
- helper-function-heavy single-pass shaders

Runtime uniforms:

- `iResolution`
- `iTime`
- `iTimeDelta`
- `iFrame`
- `iMouse`
- `iDate`

Common sanitizer fixes:

- removes conflicting built-in uniform declarations such as `uniform float iTime;`
- removes source `#version` lines and GLES precision qualifiers
- removes a few unsupported GLES extension lines
- wraps `mainImage(...)` shaders into a regular fragment `main()`
- converts `texture2D(...)` and `textureCube(...)` to `texture(...)`
- rewrites `gl_FragColor` to `FragColor`

Unsupported in V0:

- multipass Buffer A/B/C/D shaders
- sound shaders
- cubemaps
- full Shadertoy compatibility

If a shader cannot be adapted safely with these simple rules, it is skipped and logged.

## Requirements

- Windows
- Visual Studio 2022 build tools / MSBuild
- OpenGL-capable GPU / driver
- `ffmpeg` available on the system `PATH`

Bundled third-party dependencies in this repo are already wired into the Visual Studio project.

## Build

Use the repo wrapper instead of calling `MSBuild.exe` directly from PowerShell.

This repo includes a small build wrapper because some Windows/PowerShell environments expose both `Path` and `PATH`, which can break MSBuild.

From the repo root:

```powershell
.\build.cmd
```

Explicit configuration:

```powershell
.\build.cmd Debug x64 Build
```

Build output:

```text
build/x64/Debug/ShadertoyShaderSelectorAndVideoRender_V0.exe
```

## Run

From the repo root:

```powershell
.\build\x64\Debug\ShadertoyShaderSelectorAndVideoRender_V0.exe
```

Optional: pass a different workspace root as the first argument.

```powershell
.\build\x64\Debug\ShadertoyShaderSelectorAndVideoRender_V0.exe D:\MyShaderWorkspace
```

If no root is passed, the app uses the repo root.

## Folder Layout

The app creates and uses these folders under the workspace root:

```text
root/
  shaders_input/
  shaders_valid/
  shaders_selected_for_rendering/
  shaders_rendered/
  video_rendered/
  logs/
  temp/
  thumbnails/
```

What they mean:

- `shaders_input`: raw candidate shader files
- `shaders_valid`: sanitized shaders that compile and run in this runtime
- `shaders_selected_for_rendering`: currently queued shaders
- `shaders_rendered`: archive copy of shaders that rendered successfully
- `video_rendered`: final encoded videos
- `logs`: scan, compile, ffmpeg, and app logs
- `temp`: frame sequences and temporary render output
- `thumbnails`: cached thumbnail images for grid view

## Quick Start

1. Build the app with `.\build.cmd`
2. Drop candidate shaders into `shaders_input`
3. Launch the app
4. Let the initial scan complete
5. Browse shaders in preview mode or grid mode
6. Press `Space` or click queue buttons to select shaders
7. Adjust offline render settings in the UI
8. Press `R` or click `Start Batch Render`
9. Collect finished videos from `video_rendered`

## UI Overview

The app has two main viewing modes.

### Preview Mode

- shows one shader full-window
- intended for focused inspection
- live `iTime` preview
- overlay shows current shader name and queue/render state

### Grid Mode

- shows many cached thumbnails
- intended for fast browsing and discovery
- filter box for narrowing the list
- click a thumbnail to make it active
- double-click a thumbnail to jump back to preview

## Controls

Keyboard shortcuts:

- `Left Arrow`: previous shader
- `Right Arrow`: next shader
- `Space`: toggle queue selection
- `G`: toggle preview / grid view
- `F`: toggle fullscreen
- `R`: start batch render

Useful UI panels:

- `Controls`: rescan, view toggle, fullscreen toggle
- `Active Shader`: current shader details and queue state
- `Shader Grid`: thumbnail browser
- `Scan Status`: validation progress and recent rejects
- `Offline Render`: width / height / fps / duration / codec / preset / filename pattern
- `Logs`: recent runtime log lines

## Rendering Behavior

Offline rendering is deterministic.

- time is driven by frame index, not wall clock
- `time = frame / fps`
- rendering uses an offscreen framebuffer
- fullscreen state does not affect final output
- shaders are rendered to PNG frame sequences in `temp`
- `ffmpeg` encodes the final video into `video_rendered`

Default render settings:

- `3840x2160`
- `60 fps`
- `5 seconds`
- codec `libx264`
- preset `medium`

You can change these in the UI before starting a batch.

## Selection And Resume Behavior

Selection is persistent on disk.

- selecting a shader copies it into `shaders_selected_for_rendering`
- deselecting removes it from that folder

Render completion is also tracked safely:

- the app writes render state into `logs/render_manifest.tsv`
- jobs move through `queued`, `rendering`, `encoding`, `done`, or `failed`
- interrupted `rendering` / `encoding` jobs are re-queued on the next launch
- a shader is only marked done after video encoding succeeds

## Logs

Important log files:

- `logs/app.log`
- `logs/scan.log`
- `logs/compile.log`
- `logs/thumbnail.log`
- `logs/render.log`
- `logs/ffmpeg.log`
- `logs/render_manifest.tsv`

Notes:

- logs are append-only
- old failures remain in the log history even after you fix the shader or sanitizer
- always check the latest entries near the bottom

## What Counts As Valid

A shader counts as valid if it:

- is recognized as a supported single-pass fragment shader
- compiles
- links
- renders frames in this runtime without crashing

Black output is acceptable. Visual quality is not used as the validity test.

## Known Limitations

- Single-pass only
- No Shadertoy buffers / channels beyond default placeholder textures
- No texture asset import UI yet
- `iDate` is supported, but offline rendering currently uses simple deterministic timing and does not emulate the full Shadertoy environment
- Some shaders that rely on unsupported GLSL extensions or nontrivial Shadertoy semantics will still be skipped

## Repo Notes

Important source files:

- `GL_Template_V0/src/ShaderToolApp.cpp`
- `GL_Template_V0/src/ShaderCatalog.cpp`
- `GL_Template_V0/src/ShaderSanitizer.cpp`
- `GL_Template_V0/src/GlRuntime.cpp`
- `GL_Template_V0/src/ThumbnailCache.cpp`
- `GL_Template_V0/src/BatchRenderer.cpp`

Build helpers:

- `build.cmd`
- `build.ps1`

## Troubleshooting

If the app opens but no shaders appear:

- make sure files are inside `shaders_input`
- click `Rescan Input`
- inspect `logs/scan.log` and `logs/compile.log`

If rendering does not start:

- confirm `ffmpeg` is installed and on `PATH`
- check `logs/ffmpeg.log`

If a shader works on Shadertoy but fails here:

- remember this app supports single-pass only
- inspect the sanitized copy in `shaders_valid`
- inspect the latest error in `logs/compile.log`

If PowerShell/MSBuild behaves strangely:

- use `.\build.cmd`
- avoid calling `MSBuild.exe` directly unless you know your environment is clean
