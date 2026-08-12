from pathlib import Path

path = Path("src/models/ppmd.cpp")
text = path.read_text()
if text.count("MAP_SHARED") != 2:
    raise SystemExit("unexpected PPMD mmap layout")
text = text.replace("MAP_SHARED", "MAP_PRIVATE")
old = "if (mmap_to_disk && counter_ % 20000 == 0) {"
if text.count(old) != 1:
    raise SystemExit("unexpected PPMD remap condition")
text = text.replace(old, "if (false && mmap_to_disk && counter_ % 20000 == 0) {")
path.write_text(text)
print("patched PPMD measurement arena to stable MAP_PRIVATE mapping")
