import time


def fib(n):
    if n < 2:
        return n
    return fib(n - 1) + fib(n - 2)


t0 = time.perf_counter()
result = fib(32)
elapsed = time.perf_counter() - t0
print("RESULT:" + str(result))
print("TIME:" + str(elapsed))
