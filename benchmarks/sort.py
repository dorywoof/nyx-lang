import time

n = 2000
a = []
x = 1
for i in range(n):
    x = (x * 1103515245 + 12345) % 2147483648
    a.append(x % 100000)

t0 = time.perf_counter()
for i in range(n):
    for j in range(n - i - 1):
        if a[j] > a[j + 1]:
            a[j], a[j + 1] = a[j + 1], a[j]
elapsed = time.perf_counter() - t0

print("FIRST:" + str(a[0]))
print("LAST:" + str(a[n - 1]))
print("TIME:" + str(elapsed))
