/*
 * Composition root (docs/ARCHITECTURE.md). It constructs the adapters and
 * starts the runtime; nothing else constructs.
 *
 * Phase 1 is the build shape only: no adapters, ports, or runtime exist yet,
 * so the app starts and returns. U13 fills this file once the worker and the
 * platform adapters exist. It intentionally includes no platform header: the
 * renderer owns vita2d (src/gfx/draw.c) and each adapter owns its SDK calls.
 *
 * The UNSAFE authid is a property of the CMake target (D32), not of this file;
 * nothing here needs elevated access, and nothing here may add it.
 */
int main(void)
{
    return 0;
}
