# Third-party notices

MotionCapture's original code is licensed under [MIT](LICENSE), copyright 2026 furinasdog. This license does not replace the licenses or terms below. The repository name is MotionCapture; the compatible Unreal module name remains `HonkaiMotionCapture`.

| Component | Source and version | Original license or terms |
| --- | --- | --- |
| MediaPipe C API headers | [google-ai-edge/mediapipe v0.10.35](https://github.com/google-ai-edge/mediapipe/tree/v0.10.35/mediapipe/tasks/c) | Original Apache-2.0 declarations; [retained license](Source/ThirdParty/MediaPipe/LICENSE) |
| MediaPipe native Windows runtime | DLL from the official [MediaPipe 0.10.35 Windows x64 wheel](https://pypi.org/project/mediapipe/0.10.35/#files) | Not committed here; retain upstream distribution notices and review the [API terms](https://developers.google.com/edge/mediapipe/legal/tos) |
| Face, hand and pose task models | Fixed variants in [installation](doc/INSTALLATION.md), pinned by [runtime-artifacts.json](Scripts/runtime-artifacts.json) | Not committed here; review each model's source and terms before use or redistribution |
| OpenCV 4.5.5 | UE OpenCV/OpenCVHelper modules; [upstream](https://github.com/opencv/opencv/tree/4.5.5) | [License](Source/ThirdParty/OpenCV/LICENSE) and [bundled contributor notices](Source/ThirdParty/OpenCV/OpenCVContrib_License.txt) |
| Unreal Engine | Developer's engine installation | Epic's applicable engine agreement; not relicensed or distributed as part of this MIT source repository |

The task-loading and result-reading implementation derives from the MIT HonkaiRendering/HonkaiVTuber native tracking code, copyright 2026 furinasdog. This plugin does not include the earlier executable broker or its AppContainer isolation model.

## Distribution

The preparation script copies user-supplied local files after matching their hashes. A SHA256 match establishes identity; it does not grant permission to use or redistribute a file. Application distributors must retain the notices accompanying their actual SDK, model and engine distributions. This summary does not replace those original notices.

No character model, texture or captured media is included or licensed by this repository. The recognition models above are general-purpose inference weights, not character assets.

## Input data and SDK communication

Google's [MediaPipe API terms](https://developers.google.com/edge/mediapipe/legal/tos) describe device-side processing of input media and separate usage/performance metrics communication. The plugin does not add a media-upload API, but it imposes no network sandbox and provides no SDK telemetry opt-out. Application authors must consider upstream disclosures and applicable consent requirements. The [upstream telemetry discussion](https://github.com/google-ai-edge/mediapipe/issues/6291) provides context, not a replacement for the terms.
