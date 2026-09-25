# Module: st2110

Placeholder for the **SMPTE 2110** GhostVidStream decoder plugin.

Implement `media_module_t` from `include/media_core.h` and call
`media_register_module()` from the module's init path. Do not put protocol
types into `media_core.h`.

See `docs/modular-compatibility.md`. GhostVidStream is the shell; this module
is protocol-specific.
