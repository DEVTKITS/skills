"""Unabhängige Python-Umsetzung der fmt-Regel (CONTRACT §3 / SPEC.md §2) — gibt "Wert<TAB>Erwartung" aus.
node svg/check_fmt.mjs liest das und vergleicht mit SvgKit.fmt. Aufruf: python3 svg/test_fmt.py | node svg/check_fmt.mjs"""
import math, random

def fmt(n):
    k = math.floor(abs(n) * 100 + 0.5)
    if k == 0:
        return "0"
    s = ("-" if n < 0 else "") + str(k // 100)
    f = k % 100
    if f:
        s += "." + ("0%d" % f if f < 10 else str(f // 10) if f % 10 == 0 else str(f))
    return s

vals = [0.005, 1.005, -0.005, 2.675, 1e-7, 123456.789, 0.015, 0.995, -0.0, 0.1 + 0.2, 99.995, -99.995, 1.5, 2.5, 0.07, 0.01, 0.1, 10, 100, 1e9, -1e9, 4999999.995, 0.004999999, -2.675, 1.045, 1.055]
rnd = random.Random(20260101)
for _ in range(3000):
    vals.append(round(rnd.uniform(-5000, 5000), rnd.choice([0, 1, 2, 3, 4, 6])))
for _ in range(1000):
    vals.append(rnd.randint(-100000, 100000) / 1000.0 + 0.0005 * rnd.choice([-1, 0, 1]))
for v in vals:
    print(repr(float(v)) + "\t" + fmt(v))
