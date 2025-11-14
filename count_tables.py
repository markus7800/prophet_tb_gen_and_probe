import itertools

PIECES = ["Q", "R", "B", "N", "P"]

tables = set()

tables.add("KK")

for p1 in PIECES:        
    tables.add(f"K{p1}K")
    tables.add(f"KK{p1}")
    
    
for (p1,p2) in itertools.product(PIECES,PIECES):
    
    ps = sorted([p1,p2])
    tables.add(f"K{ps[0]}{ps[1]}K")
    tables.add(f"KK{ps[0]}{ps[1]}")
    
    tables.add(f"K{p1}K{p2}")
    
for (p1,p2,p3) in itertools.product(PIECES,PIECES,PIECES):
    
    ps = sorted([p1,p2,p3])
    tables.add(f"K{ps[0]}{ps[1]}{ps[2]}K")
    tables.add(f"KK{ps[0]}{ps[1]}{ps[2]}")
    
    ps = sorted([p1,p2])
    tables.add(f"K{ps[0]}{ps[1]}K{p3}")
    tables.add(f"K{p3}K{ps[0]}{ps[1]}")
    


for (p1,p2,p3,p4) in itertools.product(PIECES,PIECES,PIECES,PIECES):
    
    ps = sorted([p1,p2,p3,p4])
    tables.add(f"K{ps[0]}{ps[1]}{ps[2]}{ps[3]}K")
    tables.add(f"KK{ps[0]}{ps[1]}{ps[2]}{ps[3]}")
    
    ps = sorted([p1,p2,p3])
    tables.add(f"K{ps[0]}{ps[1]}{ps[2]}K{p4}")
    tables.add(f"K{p4}K{ps[0]}{ps[1]}{ps[2]}")
    
    ps1 = sorted([p1,p2])
    ps2 = sorted([p3,p4])
    tables.add(f"K{ps1[0]}{ps1[1]}K{ps2[0]}{ps2[1]}")
    

print(sorted(tables))
print(f"{len(tables)=}")

for c in range(2,6+1):
    print(c, sum(1 for t in tables if len(t) == c))

all_diff = [t for t in tables if len(t) == 6 and all(map(lambda p: len(set(p)) == len(p), t.split("K")[1:]))]
all_diff_no_pawn = [t for t in all_diff if "P" not in t]
print(f"{len(all_diff_no_pawn)=}")
all_diff_one_pawn = [t for t in all_diff if t.count("P") == 1]
print(f"{len(all_diff_one_pawn)=}")
all_diff_two_pawn = [t for t in all_diff if t.count("P") == 2]
print(f"{len(all_diff_two_pawn)=}")

one_on_five = [t for t in tables if len(t) == 6 and (t.startswith("KK") or t.endswith("K"))]
print(f"{len(one_on_five)=}")


no_pawn = [t for t in tables if  len(t) == 6 and t.count("P") == 0]
one_pawn = [t for t in tables if  len(t) == 6 and t.count("P") == 1]
two_pawn = [t for t in tables if  len(t) == 6 and t.count("P") == 2]
three_pawn = [t for t in tables if  len(t) == 6 and t.count("P") == 3]
four_pawn = [t for t in tables if  len(t) == 6 and t.count("P") == 4]
print(f"{len(no_pawn)=}")
print(f"{len(one_pawn)=}")
print(f"{len(two_pawn)=}")
print(f"{len(three_pawn)=}")
print(f"{len(four_pawn)=}")