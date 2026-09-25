# Decoder dependency

GhostVidStream embeds **libghost_ndihx** (NDI|HX receive library) from this tree
under `include/ghost_ndihx.h` and `src/modules/ghost_ndihx/`.

Standalone decoder repository (same API, library-only packaging):

- GitHub: https://github.com/iHadAThought/libghost_ndihx
- Forgejo: https://git.ghostnetwork.app/Brendan/libghost_ndihx

Prefer linking against a published `libghost_ndihx` release when integrating in
other apps; this app repo vendors the sources for a single-checkout booth build.
