# Shows one test: arguments, gcov counts and source around a line, and CBMC's core and trace.
#   python3 show.py <seed>_<k> <line> [context]
import os, re, subprocess, sys
W = os.getcwd()   # the folder run.py was run in
test, line = sys.argv[1], int(sys.argv[2])
ctx = int(sys.argv[3]) if len(sys.argv) > 3 else 4
d = os.path.join(W, "work", test)
for f in os.listdir(d):
    if f.endswith(".gcda"):
        os.remove(os.path.join(d, f))
res = subprocess.run(["./prog"], cwd=d, capture_output=True, text=True).stdout
call = re.search(r"f\((\d+u, \d+u, \d+u)\)", open(os.path.join(d, "driver.c")).read()).group(1)
print(f"test {test}: f({call}) = {res}")
g = subprocess.run(["gcov", "-t", "-o", "f.o", "f.c"], cwd=d, capture_output=True, text=True).stdout
for l in g.splitlines():
    m = re.match(r"\s*([^:]+):\s*(\d+):(.*)$", l)
    if m and abs(int(m.group(2)) - line) <= ctx and int(m.group(2)) > 0:
        print(f"{'>>' if int(m.group(2)) == line else '  '} {m.group(1).strip():>6s} {int(m.group(2)):4d}| {m.group(3)}")
out = open(os.path.join(d, "cbmc.out")).read()
core = re.search(r"^Core:(.*)$", out, re.M).group(1).split()
print("core constraints mentioning line", line, ":", [c for c in core if re.search(rf"(line{line}\.|\.{line}\.)", c)])
print("core size", len(core), "|", re.search(r"^Trace:.*$", out, re.M).group(0))
