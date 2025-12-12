import requests
from time import sleep

mates = []
with open("longest_mates.txt", "r") as f:
    count = 0
    correct = 0
    for l in f:
        egtb, _, rest = l.strip().partition(": ")
        if rest == "no win.":
            continue
        fen, _, rest = rest.partition(" mate in ")
        plies = int(rest.split()[0])
        
        mates.append((egtb, fen, plies))
        
mates = sorted(mates, key=lambda x: x[2], reverse=True)
for i in range(20):
    print(mates[i])
exit(0)

with open("longest_mates.txt", "r") as f:
    count = 0
    correct = 0
    for l in f:
        egtb, _, rest = l.strip().partition(": ")
        if rest == "no win.":
            continue
        fen, _, rest = rest.partition(" mate in ")
        plies = int(rest.split()[0])
        
        print(egtb, fen, "plies:", plies)
        # if (len(egtb) <= 5): continue
        
        res = requests.get("https://www.k4it.de/egtb/fetch.php", params={
            "action": "egtb",
            "fen": fen
        })
        if (res.status_code != 200):
            print("Error:", res.status_code)
            continue
        mate_in_text = res.text.splitlines()[0].partition("|")[2]
        print("\tvs Nalimov:", mate_in_text, end=" ")
        if "Not found" in mate_in_text:
            print("... no tablebase info")
        else:
            mate_in = int(mate_in_text.split()[-1])
            is_equal = mate_in == plies // 2 + 1
            print("... matches:", is_equal)
            count += 1
            correct += is_equal
        
        sleep(1)
        
    print(f"Total tested: {count}, Correct: {correct}, Accuracy: {correct / count:.2%}")