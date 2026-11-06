This port's I3DL2 mono DSP is a C adaptation of the Ryujinx.Audio implementation,
Copyright (c) Ryujinx Team and Contributors, under the MIT license in LICENSE.txt.
The Aux ring exchange and stereo sink use the same documented renderer protocol.

Source: https://github.com/alula/Ryujinx/tree/1df6c07f78c4c3b8c7fc679d7466f79a10c2d496/src/Ryujinx.Audio/Renderer

Exact unchanged reference sources and individual SHA-256 hashes are in
`tests/oracles/ryujinx-audio/SOURCE.json`. They are a host DSP oracle only;
the port does not embed that emulator or execute its CPU/GPU implementation.
This reference is not a physically approved NextOS port and proves no gameplay.

Adaptation is restricted to mono I3DL2 at 48 kHz. Unsupported channel counts,
nonfinite parameters and missing guest pools still stop explicitly. Native
Aux callbacks, effect ordering and parameter updates are retained. Reverb
state is private per effect and freed on replacement/renderer close.
