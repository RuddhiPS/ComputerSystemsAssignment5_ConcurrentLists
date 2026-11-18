
import csv
import matplotlib.pyplot as plt

threads = []
global_lock = []
hand_over_hand = []

with open("results.csv") as f:
    for row in csv.DictReader(f):
        threads.append(int(row["threads"]))
        global_lock.append(float(row["global"]))
        hand_over_hand.append(float(row["hand_over_hand"]))

plt.figure(figsize=(9, 5))

plt.plot(threads, global_lock, "o-", label="Global Lock")
plt.plot(threads, hand_over_hand, "s-", label="Hand-over-Hand")

plt.xlabel("Number of Threads")
plt.ylabel("Throughput (operations/second)")
plt.title("Concurrent Linked List Throughput")
plt.xticks(threads)
plt.legend()
plt.grid(True)
plt.tight_layout()

plt.savefig("throughput.png", dpi=200)
plt.show()
