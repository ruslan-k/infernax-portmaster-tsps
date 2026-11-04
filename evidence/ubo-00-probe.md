# UBO capability probe — build 31

This build changes only startup diagnostics: it includes GLES 3 declarations and records `GL_MAX_UNIFORM_BLOCK_SIZE`, `GL_MAX_UNIFORM_BUFFER_BINDINGS`, and vertex/fragment/combined uniform-block limits. No UBO upload or shader representation is enabled yet.

- Output: `build/out-all-31/infernax-nextos`
- SHA-256: `49460524e6888095c486fdea592bb09c24d52e9b60d56e6ab79f260cc1020ebd`
- Build: Docker `pmjs-arm64-spruce-local:20261002-gcc10`, exit 0.
- Build log: `build/build-all-31.log`; sanitizer confirmed the same SHA. Four existing `write()` return-value warnings in `loader.c`; no errors.
- Prior device binary before deployment: SHA-256 `f648be4d716e0702291d3fad50446fdd4a64f9ff2f603a2fb9da233178ef4760`.
- Existing device log identified Mali-G57 / OpenGL ES 3.2; the UBO limit values are not known until this probe runs through principal.
