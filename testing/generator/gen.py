# Random small C functions for comparing gcov line coverage with CBMC proof line coverage.
# Integer only and free of undefined behaviour: all values are unsigned (wrap-around is
# defined), divisors are made odd (| 1), shift amounts are constants < 4, array indices are
# masked with & 3, loops run at most 4 times. One statement per line, except deliberate
# multi-line statements.
import random

VARS = ["a", "b", "c", "x", "y", "z", "t"]

class Gen:
    def __init__(self, seed):
        self.r = random.Random(seed)
        self.lines = []
        self.depth = 0
        self.loop_depth = 0
        self.uses_goto = False

    def emit(self, text):
        self.lines.append("  " * (self.depth + 1) + text)

    def leaf(self):
        r = self.r.random()
        if r < 0.55:
            return self.r.choice(VARS)
        if r < 0.85:
            return str(self.r.randint(0, 20))
        return f"arr[{self.r.choice(VARS)} & 3]"

    def expr(self, d=0):
        if d >= 2 or self.r.random() < 0.35:
            return self.leaf()
        k = self.r.random()
        e1, e2 = self.expr(d + 1), self.expr(d + 1)
        if k < 0.35:
            return f"({e1} {self.r.choice(['+', '-', '*', '&', '|', '^'])} {e2})"
        if k < 0.45:
            return f"({e1} {self.r.choice(['<<', '>>'])} {self.r.randint(0, 3)})"
        if k < 0.55:
            return f"({e1} {self.r.choice(['/', '%'])} ({e2} | 1))"
        if k < 0.75:
            return f"({self.cond(d + 1)})"
        if k < 0.88:
            return f"({self.cond(d + 1)} ? {e1} : {e2})"
        return f"h({e1}, {e2})"

    def cmp(self, d):
        return f"{self.expr(d + 1)} {self.r.choice(['<', '<=', '>', '>=', '==', '!='])} {self.expr(d + 1)}"

    def cond(self, d=0):
        k = self.r.random()
        if k < 0.5 or d >= 2:
            return self.cmp(d)
        if k < 0.85:
            return f"({self.cmp(d)}) {self.r.choice(['&&', '||'])} ({self.cmp(d)})"
        return f"!({self.cmp(d)})"

    def assign(self):
        v = self.r.choice(["x", "y", "z", "t"])
        k = self.r.random()
        if k < 0.12:                       # statement over several lines
            self.emit(f"{v} = {self.expr()}")
            self.emit(f"    + {self.expr()};")
        elif k < 0.25:
            self.emit(f"{v} {self.r.choice(['+=', '-=', '^=', '|='])} {self.expr()};")
        elif k < 0.32:
            self.emit(f"{v}++;")
        elif k < 0.42:
            self.emit(f"arr[{self.r.choice(VARS)} & 3] = {self.expr()};")
        else:
            self.emit(f"{v} = {self.expr()};")

    def block(self, n):
        self.depth += 1
        for _ in range(n):
            self.stmt()
        self.depth -= 1

    def stmt(self):
        k = self.r.random()
        deep = self.depth >= 3
        if k < 0.45 or deep:
            self.assign()
        elif k < 0.62:
            self.emit(f"if ({self.cond()}) {{")
            self.block(self.r.randint(1, 3))
            if self.r.random() < 0.5:
                self.emit("} else {")
                self.block(self.r.randint(1, 2))
            self.emit("}")
        elif k < 0.68:                     # if with the statement on the same line
            v = self.r.choice(["x", "y", "z", "t"])
            self.emit(f"if ({self.cond()}) {v} = {self.expr()};")
        elif k < 0.78 and self.loop_depth < 2:
            i = "ij"[self.loop_depth]
            self.emit(f"for ({i} = 0; {i} < {self.r.randint(1, 4)}; {i}++) {{")
            self.loop_depth += 1
            self.depth += 1
            for _ in range(self.r.randint(1, 3)):
                if self.r.random() < 0.2:
                    self.emit(f"if ({self.cond()}) {self.r.choice(['break', 'continue'])};")
                else:
                    self.stmt()
            self.depth -= 1
            self.loop_depth -= 1
            self.emit("}")
        elif k < 0.84 and self.depth == 0:
            self.emit(f"if ({self.cond()}) return {self.expr()};")
        elif k < 0.90:
            self.emit(f"switch ({self.expr()} & 3u) {{")
            for case in (0, 1, 2):
                self.emit(f"case {case}:")
                self.block(1)
                if self.r.random() < 0.7:
                    self.emit("  break;")
            self.emit("default:")
            self.block(1)
            self.emit("}")
        elif k < 0.93 and self.depth == 0 and not self.uses_goto:
            self.uses_goto = True
            self.emit(f"if ({self.cond()}) goto out;")
        else:
            v = self.r.choice(["x", "y", "z", "t"])
            self.emit(f"{v} = ({self.cond()}) ? {self.expr()} : {self.expr()};")

    def program(self):
        for _ in range(self.r.randint(6, 14)):
            self.stmt()
        body = list(self.lines)
        out = ["unsigned h(unsigned p, unsigned q)", "{", "  if (p > q)", "    return p - q;", "  return q - p;", "}", "",
               "unsigned f(unsigned a, unsigned b, unsigned c)", "{",
               "  unsigned x = a;", "  unsigned y = b;", "  unsigned z = c;", "  unsigned t = 0;",
               "  unsigned i, j;", "  unsigned arr[4] = {1, 2, 3, 4};"]
        out += body
        if self.uses_goto:
            out += ["out:"]
        out += [f"  return {self.expr()};", "}"]
        return "\n".join(out) + "\n"

def program(seed):
    return Gen(seed).program()

if __name__ == "__main__":
    import sys
    print(program(int(sys.argv[1]) if len(sys.argv) > 1 else 1))
