import os
import pathlib

sizes = {}
for d, _, files in os.walk("egtbs"):
    for file in files:
        if file.endswith(".bz"):
            
            sizes[file[:-len(".egtb.bz")]] = pathlib.Path(d, file).stat().st_size


# print(sizes)
size = sum(sizes.values())
print(size / 1024**4)

minimal_size = 0
for egtb_id, s in sizes.items():
    # if "KK" in egtb_id: continue
    _, a, b = egtb_id.split("K")
    mirror_egtb_id = f"K{b}K{a}"
    if egtb_id != mirror_egtb_id:
        if egtb_id < mirror_egtb_id:
            minimal_size += min(s, sizes[mirror_egtb_id])
    else:
        minimal_size += s
print(minimal_size / 1024**4)