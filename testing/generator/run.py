# Compares gcov line coverage with CBMC proof line coverage on random programs from gen.py.
#   python3 run.py <first seed> <number of programs> [inputs per program]
# For each program and input: gcc --coverage + gcov (executed lines), CBMC with
# --unwinding-assertions (bound check), CBMC --proof-coverage --proof-coverage-trace (one core).
# Every line that differs is classified; results go to results.json in the current folder, the
# files of each test stay in work/<seed>_<k>/ in the current folder.
import collections, json, multiprocessing, os, random, re, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
W = os.getcwd()
sys.path.insert(0, HERE)
from gen import program

# the proof-coverage binary of this repository (testing/generator/../../build/bin/cbmc)
CBMC = os.environ.get("CBMC", os.path.join(HERE, "..", "..", "build", "bin", "cbmc"))
UNWIND = "6"
TIMEOUT = 60

def sh(cmd, cwd, timeout=TIMEOUT):
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, errors="replace", timeout=timeout)

# ---------------------------------------------------------------- classification
HARNESS_LINE = 3
LABEL = re.compile(r"^\s*(case\b.*|default\s*):\s*$")
HEADER = re.compile(r"^[\w\s\*]*\b(\w+)\s*\([^;]*\)\s*\{?$")

def gcov_only_reason(n, text, lines, proof_only):
    s = text.strip()
    h = HEADER.match(s)
    if s in ("{", "}", "} else {") or (h and h.group(1) not in ("if", "while", "for", "switch")):
        return "entry/exit"
    if re.match(r"switch\s*\(", s):
        return "switch line"
    if LABEL.match(text):
        return "case label"
    if re.match(r"(break|continue|goto\s+\w+)\s*;$", s) or re.match(r"\w+\s*:$", s):
        return "jump/label"
    if re.match(r"\w+\s*\(.*\)\s*;$", s) and not re.match(r"(if|while|for|switch|return)\b", s):
        return "call statement"
    # an earlier line of a statement whose last line the proof covers
    m = n
    while m < len(lines) and not lines[m - 1].rstrip().endswith((";", "{", "}")):
        m += 1
    if m != n and proof_only.get(m) == "statement line":
        return "multi-line statement"
    return "not needed"

def proof_only_reason(n, text, core, count, not_executed):
    if n in not_executed:
        return "non-trace"
    if re.match(r"\s*(while\s*\(\s*1\s*\)|for\s*\(\s*;\s*;\s*\))", text):
        return "constant condition"
    # constraint names carry no file; harness.c has its statements on line 3
    if n != HARNESS_LINE and any(re.fullmatch(rf"line{n}\.\d+", c) for c in core):
        if count == "-":
            return "statement line"
        if not text.rstrip().endswith(";"):
            return "multi-line statement"   # gcc puts the executed part on another line
        return "statement not executed"
    if count == "-":
        return "no gcc code"          # gcc folded the code away (e.g. a > a), CBMC did not
    if LABEL.match(text):
        return "case label"
    phis = any(re.match(rf"phi\.(if|else)\.{n}\.", c) for c in core)
    if phis and text.strip() == "}":
        return "function exit merge"
    if phis:
        return "branch merge"
    if re.search(r"\b(if|while|for|switch)\s*\(|\?|\bcase\b|\bdefault\b", text):
        return "condition attribution"
    return "unexplained"

# ---------------------------------------------------------------- one test
def run_test(job):
    seed, k = job
    src = program(seed)
    rnd = random.Random(seed * 1000 + k)
    args = [rnd.choice([0, 1, 2, 3, 5, 7, 10, 13, 19, 20, 21, 100, 255, 1000]) for _ in range(3)]
    d = os.path.join(W, "work", f"{seed}_{k}")
    os.makedirs(d, exist_ok=True)
    open(os.path.join(d, "f.c"), "w").write(src)
    open(os.path.join(d, "driver.c"), "w").write(
        "#include <stdio.h>\nunsigned f(unsigned a, unsigned b, unsigned c);\n"
        f"int main(void) {{ printf(\"%u\", f({args[0]}u, {args[1]}u, {args[2]}u)); return 0; }}\n")
    res = {"seed": seed, "k": k, "args": args}
    t = time.perf_counter()
    for cmd in (["gcc", "-O0", "-w", "--coverage", "-c", "f.c", "-o", "f.o"],
                ["gcc", "-O0", "-w", "-c", "driver.c", "-o", "driver.o"],
                ["gcc", "--coverage", "f.o", "driver.o", "-o", "prog"]):
        if sh(cmd, d).returncode:
            return dict(res, status="build failed")
    run = sh(["./prog"], d, timeout=10)
    if run.returncode or not run.stdout.strip().isdigit():
        return dict(res, status="native run failed")
    result = run.stdout.strip()
    g = sh(["gcov", "-t", "-o", "f.o", "f.c"], d).stdout
    res["gcov_time"] = time.perf_counter() - t
    executed, annotated = set(), {}
    for l in g.splitlines():
        m = re.match(r"\s*([^:]+):\s*(\d+):(.*)$", l)
        if m and int(m.group(2)) > 0:
            c, n = m.group(1).strip(), int(m.group(2))
            annotated[n] = c
            if c != "-" and not c.startswith("#") and not c.startswith("="):
                executed.add(n)
    open(os.path.join(d, "harness.c"), "w").write(
        "#include <assert.h>\nunsigned f(unsigned a, unsigned b, unsigned c);\n"
        f"void harness(void) {{ unsigned r = f({args[0]}u, {args[1]}u, {args[2]}u); assert(r == {result}u); }}\n")
    base = [CBMC, "f.c", "harness.c", "--function", "harness", "--unwind", UNWIND]
    try:
        v = sh(base + ["--unwinding-assertions", "--no-standard-checks"], d)
        t = time.perf_counter()
        p = sh(base + ["--proof-coverage", "--proof-coverage-annotate", "--proof-coverage-trace"], d)
        res["cbmc_time"] = time.perf_counter() - t
    except subprocess.TimeoutExpired:
        return dict(res, status="timeout")
    out = p.stdout + p.stderr
    open(os.path.join(d, "cbmc.out"), "w").write(out)
    if "VERIFICATION SUCCESSFUL" not in v.stdout:
        return dict(res, status="unwind bound too small" if "unwinding assertion" in v.stdout else "verification failed")
    if "Test fail" in out:
        return dict(res, status="CBMC cannot prove gcc's result")
    if "Test pass" not in out:
        return dict(res, status="cbmc error", detail=out.strip().splitlines()[-1:])
    m = re.search(r"^Covered lines:(.*)$", out, re.M)
    proof = {int(i.rpartition(":")[2]) for i in m.group(1).split() if i.rpartition(":")[0] == "f.c"}
    core = (re.search(r"^Core:(.*)$", out, re.M) or [None, ""])[1].split()
    tr = re.search(r"^Trace: (\S+)(?: \(not executed by the run: (.*)\))?$", out, re.M)
    # statement lines and branches with an untaken side the proof uses
    not_executed = {int(x) for x in re.findall(r"line (\d+)", tr.group(2) or "")} if tr else set()
    lines = src.split("\n")
    proof_only = {n: proof_only_reason(n, lines[n - 1], core, annotated.get(n, "-"), not_executed)
                  for n in sorted(proof - executed)}
    gcov_only = {n: gcov_only_reason(n, lines[n - 1], lines, proof_only) for n in sorted(executed - proof)}
    return dict(res, status="pass", result=result, executed=sorted(executed), proof=sorted(proof),
                trace=tr.group(1) if tr else None, proof_only=proof_only, gcov_only=gcov_only,
                soft=int(re.search(r"Proof coverage: (\d+) soft", out).group(1)) if "Proof coverage:" in out else None)

if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit("usage: python3 run.py <first seed> <number of programs> [inputs per program]")
    if not os.access(CBMC, os.X_OK):
        sys.exit(f"no CBMC binary at {CBMC}: build it first, or set CBMC=/path/to/cbmc")
    first, count = int(sys.argv[1]), int(sys.argv[2])
    per = int(sys.argv[3]) if len(sys.argv) > 3 else 2
    jobs = [(s, k) for s in range(first, first + count) for k in range(per)]
    t = time.perf_counter()
    with multiprocessing.Pool(os.cpu_count()) as pool:
        results = pool.map(run_test, jobs, chunksize=4)
    json.dump({"jobs": len(jobs), "wall": time.perf_counter() - t, "results": results},
              open(os.path.join(W, "results.json"), "w"), indent=1)
    print(f"{len(jobs)} tests in {time.perf_counter() - t:.0f}s")
    statuses = collections.Counter(r["status"] for r in results)
    print("status:", ", ".join(f"{k} {v}" for k, v in statuses.most_common()))
    kinds = collections.Counter(v for r in results for v in set(r.get("proof_only", {}).values()))
    print("tests with proof-only lines, by kind:",
          ", ".join(f"{k} {v}" for k, v in kinds.most_common()) or "none")
