import time

n = 20000
t0 = time.perf_counter()
s = ""
for i in range(n):
    s = s + "x"
elapsed = time.perf_counter() - t0

print("LEN:" + str(len(s)))
print("TIME:" + str(elapsed))
