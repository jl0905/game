#pragma once

// Headless script harness: runs the full game simulation without a window,
// driven by a command script (see harness.cpp for the command reference).
// `path` is a script file, or "-" for stdin. Returns a process exit code.
int RunScript(const char* path);
