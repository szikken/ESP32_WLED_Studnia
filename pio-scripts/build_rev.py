# Pre-build script: increment build revision counter.
# The counter is stored in .build_rev (project root) and read by set_metadata.py
# which appends "_r<rev>" to WLED_RELEASE_NAME when compiling wled_metadata.cpp.
# e.g. "ESP32_Studnia" → "ESP32_Studnia_r5" — visible in WLED web UI Info panel
# and used as the output .bin filename suffix by output_bins.py.
Import('env')
import os


def _get_next_rev(project_dir):
    rev_file = os.path.join(project_dir, ".build_rev")
    rev = 1
    if os.path.isfile(rev_file):
        try:
            with open(rev_file, "r") as f:
                rev = int(f.read().strip()) + 1
        except (ValueError, IOError):
            rev = 1
    with open(rev_file, "w") as f:
        f.write(str(rev))
    return rev


# Detect clean/fullclean targets to avoid wasting rev numbers.
from SCons.Script import BUILD_TARGETS
_is_clean = any(str(t) in ('clean', 'fullclean', 'erase') for t in BUILD_TARGETS)

if not _is_clean:
    _rev = _get_next_rev(env["PROJECT_DIR"])
    print(f"[build_rev] Build revision {_rev}")
