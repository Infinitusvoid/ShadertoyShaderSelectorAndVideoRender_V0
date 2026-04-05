#include <iostream>
#include <vector>
#include <string>
#include <random>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <algorithm>
#include <cstdlib> // std::atoi

// -----------------------------
// OpenGL / GLFW / GLEW
// -----------------------------
#include <GL/glew.h>
#include <GLFW/glfw3.h>

// -----------------------------
// stb_image & stb_image_write
// -----------------------------
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../External_libs/stb/image/stb_image.h"
#include "../External_libs/stb/image/stb_image_write.h"

// -----------------------------
// tinyply (header-only style)
// -----------------------------
// #define TINYPLY_IMPLEMENTATION
// #include "../External_libs/tinyply/tinyply/source/tinyply.h"

#include "../cpp_httplib/cpp-httplib-master/httplib.h"

#include "../External_libs/glm_0_9_9_7/glm/glm/glm.hpp"

int main()
{
	const std::string folder_root = "C:/Users/Cosmos/Desktop/output/ShadertoyShaderSelectorAndVideoRender_V0";
	std::string shader_folder_path = folder_root + "/shaders_input";
	
	std::string shader_folder_path_valid = folder_root + "/shaders_valid"; // The shaders are valid shadertoy shader they compile and run nicely

	std::string shader_selected_for_rendering_folder_path = folder_root + "/shaders_selected_for_rendering";

	std::string shader_rendered = folder_root + "/shaders_rendered";
	std::string video_rendered = folder_root + "/video_rendered";

	/*
	Build a robust desktop C++ application for selecting, previewing, sanitizing, and offline-rendering single-pass Shadertoy-style fragment shaders into videos.

Important: do not spend time writing long design docs or endless plans. Write working code incrementally and keep the code modular, explicit, and practical. The priority is a usable working tool, not documentation theater.

The application is a shader selector and renderer for a local shader library. It must be robust, pleasant to use, and able to skip bad shaders without breaking the workflow.

==================================================
HIGH-LEVEL GOAL
==================================================

Build a native desktop application that:

1. Scans a folder of candidate shader files.
2. Tries to interpret each file as a single-pass Shadertoy-like fragment shader.
3. Sanitizes / adapts shaders when possible so they work in our runtime.
4. Keeps only working shaders in a valid folder.
5. Lets the user browse valid shaders visually.
6. Supports both:
   - a single-shader preview mode
   - a thumbnail grid view for fast selection
7. Lets the user mark shaders for offline rendering.
8. Renders selected shaders deterministically offscreen into high-quality video files.
9. Uses ffmpeg from the system PATH for video encoding.
10. Is crash-tolerant and resume-friendly.

This is not a general Shadertoy clone. It is a robust local shader triage, selection, and rendering pipeline.

==================================================
IMPORTANT SCOPE RULES
==================================================

V0 scope:

- Desktop C++ app.
- GLFW + OpenGL for rendering/windowing is fine.
- GLEW is fine.
- stb_image / stb_image_write are fine.
- ImGui is allowed and encouraged for overlays, controls, grid view, and status panels.
- ffmpeg is available on the system PATH and should be invoked as an external process.
- Single-pass fragment shaders only.
- No sound shaders.
- No multipass Buffer A/B/C/D support.
- No cubemap shaders.
- No browser/web UI for V0.
- No remote server architecture for V0.
- No attempt to perfectly emulate all of Shadertoy.
- Robust local workflow matters more than feature completeness.

If a shader cannot be safely adapted into our supported single-pass runtime, skip it, log the reason, and continue.

==================================================
SUCCESS CRITERIA
==================================================

The program is successful if it can:

- scan a folder of mixed candidate files,
- ignore clearly unsupported files,
- compile and run many single-pass shaders,
- automatically sanitize common shader issues,
- preserve a smooth workflow even when many shaders fail,
- show a grid view with thumbnails for fast browsing,
- allow quick selection of shaders to render,
- render selected shaders deterministically offscreen,
- encode videos through ffmpeg,
- survive bad inputs without crashing,
- preserve progress if the app crashes during batch rendering.

Black output is acceptable. A shader does NOT have to look visually good to count as valid. If it compiles, links, runs in the supported wrapper, and renders frames without crashing, it is acceptable even if the result is black or boring.

==================================================
SUPPORTED SHADER MODEL
==================================================

Support single-pass image-style shaders in Shadertoy-like style.

The runtime should support common built-ins such as:

- iResolution
- iTime
- iTimeDelta
- iFrame
- iMouse

Optionally support iDate if easy, but not required for V0.

The shader may come in forms such as:

1. Proper Shadertoy style:
   void mainImage(out vec4 fragColor, in vec2 fragCoord)

2. A shader with helper functions and mainImage

3. A shader with a plain GLSL main() fragment body

The app should wrap/adapt these into the runtime when safe.

==================================================
SANITIZATION / ADAPTATION RULES
==================================================

This part is extremely important.

We want robustness, not perfection. Apply simple, deterministic, safe fixes that preserve the shader as much as possible. Do not attempt deep semantic rewriting.

Allowed kinds of fixes include:

- removing duplicate or conflicting built-in uniform declarations that our wrapper already provides
- removing bad declarations like custom declarations of iTime / iResolution if they conflict with engine-provided uniforms
- adapting common Shadertoy-style code into our fragment wrapper
- adding a wrapper so mainImage-based shaders work in regular GLSL fragment execution
- converting safe legacy syntax when trivial
- stripping harmless metadata/comments around copied shaders
- handling text files with odd extensions if the contents look shader-like

Very important:
some local shaders may contain lines such as:
- uniform float iTime;
- uniform float iResolution;
- uniform vec2 iResolution;
or other incorrect/redundant declarations.

If the shader otherwise looks like a single-pass shader, prefer removing or neutralizing these conflicting built-in declarations and replacing them with the runtime-provided definitions rather than rejecting the shader immediately.

If a shader cannot be made safe and valid with simple deterministic fixes, skip it and log exactly why it was skipped.

Do NOT build a magical AI shader repair engine. Build a robust rule-based sanitizer for common cases.

==================================================
WHAT COUNTS AS A VALID SHADER
==================================================

A shader counts as valid if:

- it is interpreted as a supported single-pass fragment shader,
- it compiles successfully,
- it links successfully,
- it renders frames in our runtime without crashing the app.

It may render black. That is fine.
It may be visually boring. That is fine.
It does not need to match Shadertoy perfectly.
Robust execution matters more than artistic output.

==================================================
INPUT / OUTPUT FOLDER STRUCTURE
==================================================

Use a root folder like this:

root/
  shaders_input/
  shaders_valid/
  shaders_selected_for_rendering/
  shaders_rendered/
  video_rendered/
  logs/
  temp/
  thumbnails/

Behavior:

- shaders_input:
  source candidate files

- shaders_valid:
  sanitized normalized versions of shaders that successfully compile and run in our runtime

- shaders_selected_for_rendering:
  shaders chosen by the user for offline video render

- shaders_rendered:
  shaders already successfully rendered to final output

- video_rendered:
  final encoded videos

- logs:
  scan logs, sanitizer logs, compile errors, render logs

- temp:
  temporary intermediate files, frame sequences if needed, incomplete outputs

- thumbnails:
  cached preview thumbnail images for grid view

Normalize valid shaders into a consistent extension such as .glsl if helpful.

==================================================
FILE SCANNING RULES
==================================================

The input folder may contain many file types.

The scanner should:

- accept likely text-based shader candidates even if the extension is unusual
- accept common shader-like extensions such as .glsl, .frag, .txt, .toy
- ignore clearly unsupported media files such as .png, .jpg, .jpeg, .avi, .mp4, .mpeg, .gif, etc.
- ignore folders or unrelated binary files
- not crash on weird encodings or bad text; log and skip if needed

Prefer practical robustness over cleverness.

==================================================
UI / USER EXPERIENCE
==================================================

The application should be fast and pleasant to use.

It must support two main browsing modes:

1. Single preview mode
   - show one shader large
   - left/right arrow goes to previous/next shader
   - space toggles selection for rendering
   - F toggles fullscreen
   - R starts batch rendering mode
   - show current shader filename and status overlay

2. Grid view
   - show many shader thumbnails at once
   - allow fast browsing and quick discovery of nice shaders
   - support scrolling
   - selecting a thumbnail makes it active in preview
   - allow multi-selection / toggle selection
   - show whether a shader is already selected or already rendered

Grid view is very important. This is a major feature, not an afterthought.

Thumbnails can be generated by rendering a preview frame or short deterministic preview sample and caching the result to disk.

The UI should clearly show:

- current shader name
- selected/unselected status
- valid/sanitized status
- already rendered status
- compile error info if invalid
- render queue progress during batch mode

ImGui is welcome if it makes this easier and cleaner.

==================================================
THUMBNAIL GENERATION
==================================================

Implement a thumbnail system for valid shaders.

A thumbnail can be generated by:
- rendering the shader offscreen at a small resolution,
- using a deterministic preview time,
- saving the result as an image in the thumbnails folder.

Cache thumbnails so they are not regenerated unnecessarily.

If thumbnail generation fails for a shader that otherwise works in normal preview, log it and continue.

==================================================
PREVIEW EXECUTION RULES
==================================================

The real-time preview should:

- run the current shader interactively in a window
- support helper functions and non-trivial shader structure
- provide runtime values for built-ins like iTime and iResolution
- remain stable while browsing quickly
- not freeze when switching shaders
- recover gracefully from compile failures

When changing shaders, the app should:
- unload old GL program safely
- compile/link the new one
- if compilation fails, show error message in UI and continue to next/other shader without crashing

==================================================
RENDER SELECTION WORKFLOW
==================================================

Selection behavior must be explicit and persistent.

Pressing space or clicking selection in the grid should toggle whether the shader is selected for offline rendering.

Selection should be represented both in memory and on disk by mirroring/copying the chosen valid shader into shaders_selected_for_rendering.

If the shader is deselected, remove it from shaders_selected_for_rendering.

This makes the workflow resumable and visible even outside the app.

==================================================
OFFLINE RENDERING MODE
==================================================

Offline rendering must be deterministic and offscreen.

Important rules:

- Do not use real-time wall clock as the render timeline.
- Advance time deterministically by exact frame step:
  time = frame_index / fps
- Render to offscreen framebuffer at target output resolution.
- Fullscreen/window mode must not affect output.
- Batch rendering should continue even if one shader fails.
- On failure, log the shader, skip it, continue to next.

User-configurable settings should include at least:

- width
- height
- fps
- duration_seconds
- output codec / ffmpeg preset if practical
- output filename pattern if practical

Default target:
- 3840x2160
- 60 fps

The duration per shader should be explicit and configurable.
For example:
- 5 seconds
- 10 seconds
- 30 seconds
etc.

The selected shader list should carry enough information that the batch render can use the chosen render settings or current settings in a clear way.

If it is easy, attach render job metadata like duration/fps/resolution to the render task. If not, it is acceptable for V0 to use the current global settings for all selected shaders.

==================================================
FFMPEG INTEGRATION
==================================================

Use ffmpeg from the system PATH.

The app should:

- detect whether ffmpeg is callable
- if not found, show a clear user-facing error message
- log the exact ffmpeg command being used
- log stdout/stderr from ffmpeg when encoding fails

Prefer a practical and robust path such as:

Option A:
- render frames to temp image sequence
- call ffmpeg to encode sequence into final video

Option B:
- pipe raw frames into ffmpeg if implemented cleanly and robustly

Choose the simpler robust solution first.
Reliability matters more than elegance.

If using image sequences first:
- keep temp files in temp/
- clean them when encoding succeeds
- preserve them if encoding fails and this helps debugging

==================================================
CRASH SAFETY / RESUME SAFETY
==================================================

This is very important.

The app must be resilient to interruption.

Rules:

- Never mark a shader as fully rendered until the final video file has been encoded successfully.
- Write incomplete output to temp names first.
- Only move a shader from shaders_selected_for_rendering to shaders_rendered after final success.
- If the app crashes midway, restarting it should not lose already completed work.
- Keep a render manifest or job log that records states like:
  - queued
  - rendering
  - encoding
  - done
  - failed

On restart:
- detect incomplete jobs
- do not corrupt previous successful outputs
- allow the user to continue rendering the remaining queued shaders

==================================================
LOGGING
==================================================

Logging must be first-class.

Create logs for:

- scanning
- ignored files
- sanitizer transformations
- compile errors
- link errors
- thumbnail generation
- selection changes
- render job start/end
- ffmpeg command lines
- ffmpeg failures
- job completion

Prefer plain text logs plus optionally simple JSON/CSV manifests if helpful.

Each log entry should include enough detail to debug failures later.

==================================================
PERFORMANCE / RESPONSIVENESS
==================================================

The app should remain usable.

During browsing:
- switching shaders should feel responsive
- cached thumbnails should make grid view fast

During rendering:
- the app should continue pumping window events
- the UI should remain responsive enough to show progress
- it should not appear hard-frozen
- batch progress should be visible

Perfect background scheduling is not required for V0, but do not design it as a total lock-up process.

==================================================
ARCHITECTURE REQUIREMENTS
==================================================

Write the code in a modular way.

Suggested modules:

- file scanning / candidate discovery
- shader text loading
- shader sanitization / normalization
- shader compilation / GL program management
- preview runtime
- thumbnail generation
- selection management
- render job queue
- offscreen renderer
- ffmpeg invocation
- logging
- app UI / input handling

Keep the code explicit and debuggable.
Avoid giant monolithic functions where possible.

==================================================
EXAMPLE OF A SUPPORTED SHADER
==================================================

This kind of shader should work:

// Experiment with shaders on shadertoy

vec2 wave(vec2 uv)
{
    return uv * 2.0 + vec2(sin(uv.x * 3.0 + iTime * 0.1), cos(uv.y * 4.0 + iTime * 0.2));
}

float palette(float t)
{
    t = fract(t * 0.5);
    return 0.5 + 0.5 * cos(6.28319 * t);
}

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    vec2 uv = fragCoord / iResolution.xy;

    float angle = iTime * 0.5 + uv.x * uv.y;
    mat2 rotationMatrix = mat2(cos(angle), -sin(angle), sin(angle), cos(angle));
    uv = rotationMatrix * uv;
    uv = wave(uv);

    float val = sin(uv.x * 10.0 + iTime) * cos(uv.y * 15.0 + iTime);
    vec3 col = vec3(palette(val), palette(val + 0.3), palette(val + 0.6));

    fragColor = vec4(col, 1.0);
}

Also support similar shaders even if they contain redundant or conflicting built-in uniform declarations that need to be removed or normalized.

==================================================
IMPLEMENTATION PRIORITY
==================================================

Build in this order:

1. Basic app skeleton with window + OpenGL + shader preview.
2. Folder scanning and candidate loading.
3. Shader sanitization / adaptation rules.
4. Valid shader pipeline and copying to shaders_valid.
5. Preview mode with next/previous browsing.
6. Grid view with thumbnails and selection.
7. Selection persistence via shaders_selected_for_rendering.
8. Offscreen deterministic renderer.
9. ffmpeg encoding.
10. Crash-safe manifest / resume logic.
11. Cleanup and polish.

At each stage, prefer working code over big promises.

==================================================
VERY IMPORTANT EXECUTION STYLE
==================================================

Do not get stuck writing endless documentation.

Write the code.
Keep it incremental.
Keep it modular.
Keep it practical.
Make reasonable decisions and implement them.
When something is unsupported, skip it safely and log it.
Do not overcomplicate V0.

The final result should be a usable robust local shader selector and renderer with grid browsing, preview, sanitization of common Shadertoy-style issues, deterministic offscreen rendering, and ffmpeg video output.
	*/

	std::cout << "GL_Template_V0\n";

	return 0;
}