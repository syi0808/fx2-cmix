from pathlib import Path

path = Path("src/models/ppmd.cpp")
text = path.read_text()

# fx2 uses a 14,000 MiB file-backed PPMD arena with MAP_SHARED. That is correct
# for the production single process, but it defeats fork() copy-on-write: a
# counterfactual child writes directly into the parent's PPMD heap. For oracle
# runs only, use a private file-backed mapping so dirty pages are isolated by
# fork without requiring a 14 GiB anonymous allocation.
shared_count = text.count("MAP_SHARED")
if shared_count != 2:
    raise SystemExit(f"expected 2 MAP_SHARED occurrences, found {shared_count}")
text = text.replace("MAP_SHARED", "MAP_PRIVATE")

# Production periodically unmaps/remaps the shared file every 20k bytes. With a
# private mapping those writes are intentionally not persisted to the backing
# file, so remapping would discard the learned PPMD state. Keep the mapping for
# the lifetime of the oracle process instead.
old = "if (mmap_to_disk && counter_ % 20000 == 0) {"
if text.count(old) != 1:
    raise SystemExit("unexpected PPMD remap condition")
text = text.replace(old, "if (false && mmap_to_disk && counter_ % 20000 == 0) {")

path.write_text(text)
print("patched PPMD oracle arena: MAP_PRIVATE, periodic remap disabled")
