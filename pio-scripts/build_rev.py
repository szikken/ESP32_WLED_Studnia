# Pre-build script: increment build revision counter and embed it into WLED_RELEASE_NAME.
# e.g. "ESP32_Studnia" → "ESP32_Studnia_r5"
# The revision is then visible both in the WLED web UI Info panel and in the output .bin filename.
Import('env')
import os


def _get_next_rev():
    rev_file = os.path.join(env["PROJECT_DIR"], ".build_rev")
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


rev = _get_next_rev()

# Append " r{rev}" to WLED_RELEASE_NAME so it is embedded in the firmware and shown in the web UI.
# CPPDEFINES values for string macros are stored as \"Name\" (with backslash-escaped quotes).
new_defines = []
found = False
for define in env["CPPDEFINES"]:
    if isinstance(define, (list, tuple)) and define[0] == "WLED_RELEASE_NAME":
        found = True
        # Strip all quote forms the value may use (\"Name\" or "Name")
        name = str(define[1]).replace('\\"', '').replace('"', '').strip()
        new_value = f'\\"{name}_r{rev}\\"'
        new_defines.append(["WLED_RELEASE_NAME", new_value])
    else:
        new_defines.append(define)

if not found:
    print(f"[build_rev] WARNING: WLED_RELEASE_NAME not found in CPPDEFINES — rev not embedded in firmware")
else:
    print(f"[build_rev] Build revision {rev} embedded into WLED_RELEASE_NAME")

env.Replace(CPPDEFINES=new_defines)
