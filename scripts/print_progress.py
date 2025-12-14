import os
from datetime import datetime
files = [f[:-9] for _,_,files in os.walk("egtbs") for f in files if f.endswith(".zip")]
for i in range(2,7):
    print(f"{i}: {sum(1 for f in files if len(f) == i):3d}")
print("  ", len(files), "   ", datetime.now())

for folder,_,files in os.walk("egtbs"):
    if not folder.endswith("pawns"): continue
    print(f"{folder} {len([f for f in files if f.endswith(".zip")]):3d}")
    