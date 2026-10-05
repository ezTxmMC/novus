#!/usr/bin/env python3
"""Differential test of the typed code generator.

Writes random Novus programs full of typed arithmetic - integer and float
locals, parameters and returns of methods, fields of classes, math calls,
mixed integer/float/untyped operands, ill-typed calls - and runs each one
through two compilers, a reference and the one under test. Output (stdout,
stderr) and exit code must be the same; a program that stops with a runtime
error is a result too (each case runs inside tryRun, so the error text is
compared as well, as is the sign of a zero).

    tools/difftyped.py REFERENCE NEW [--count N] [--seed S] [--jobs J] [--keep DIR] [--strict]

A difference that only is the sign of a NaN, the sign of a zero (the reference
built by gcc 16 prints some `0.0 - (double)n` as -0.000000) or which of two
failing operands reports its error first is counted by itself and kept in DIR
as .softdiff; --strict makes those failures too.

REFERENCE and NEW are `novusc` binaries. The reference is the compiler before
the typed code generation (git show <commit>:bootstrap/novusc.c, built with
cc). Programs that differ are kept in DIR (default: ./difftyped-failures).
The C compiler that builds the programs is $NOVUS_CC (clang finds what gcc
does not: it fuses floating point operations unless told not to, and orders
the operands of a call the other way round).

Known, intended differences are kept out of the programs: a float `%` by zero,
an integer expression assigned to a float local that divides, NaN
comparisons, and stores of a wrongly typed value into a numeric field.
"""
import argparse
import concurrent.futures
import os
import random
import shutil
import subprocess
import sys
import tempfile

INT_LITERALS = ["0", "1", "2", "3", "7", "-1", "-4", "10", "100", "1000000", "2147483648", "1099511627776"]
FLOAT_LITERALS = ["0.0", "0.5", "1.5", "-2.25", "3.14159", "100.0", "0.001", "-0.75", "12345.678"]
STRING_LITERALS = ['"a"', '"12"', '"x y"', '""', '"3.5"']
PARAM_TYPES = ["integer", "float", "object", "string", "array<float>"]
SHIFT_COUNTS = ["0", "1", "3", "-1", "-3"]


class Scope:
    """The names an expression may use: name -> kind ('I', 'F', 'O', 'S')."""

    def __init__(self):
        self.vars = {}

    def of(self, kind):
        return [n for n, k in self.vars.items() if k == kind]


class Generator:
    def __init__(self, rng):
        self.rng = rng
        self.functions = []  # (name, [(type, name)], ret)
        self.temp = 0

    # ------------------------------------------------------------ expressions

    def pick(self, items):
        return self.rng.choice(items)

    def elements(self, scope):
        """Untyped operands: variables of unknown type and elements of arrays."""
        return scope.of("O") + ["%s[%d]" % (a, k) for a in scope.of("L") for k in (0, 1, 2)]

    def int_expr(self, scope, depth, no_div=False):
        r = self.rng
        names = scope.of("I")
        if depth <= 0 or r.random() < 0.25:
            if names and r.random() < 0.6:
                return self.pick(names)
            return self.pick(INT_LITERALS)
        c = r.random()
        if c < 0.45:
            op = self.pick(["+", "-", "*", "+", "-", "&", "|", "^"] + ([] if no_div else ["/", "%"]))
            left = self.int_expr(scope, depth - 1, no_div)
            right = self.int_expr(scope, depth - 1, no_div)
            if op == "*":
                right = self.pick(["2", "3", "-1", "0", "7"]) if r.random() < 0.7 else right
            return "(%s %s %s)" % (left, op, right)
        if c < 0.52:
            return "(%s %s %s)" % (self.int_expr(scope, depth - 1, no_div), self.pick(["<<", ">>"]), self.pick(SHIFT_COUNTS))
        if c < 0.58:
            return "(-%s)" % self.int_expr(scope, depth - 1, no_div)
        if c < 0.80:
            call = self.call_of(scope, depth, "integer")
            if call:
                return call
        if c < 0.82:
            return "math.%s(%s)" % (self.pick(["floor", "ceil", "round"]), self.float_expr(scope, depth - 1))
        if c < 0.88 and scope.of("A"):
            return "%s.ia()" % self.pick(scope.of("A"))
        if c < 0.92 and scope.of("L"):
            return "%s.length()" % self.pick(scope.of("L"))
        return self.int_expr(scope, depth - 1, no_div)

    def float_expr(self, scope, depth):
        r = self.rng
        names = scope.of("F")
        if depth <= 0 or r.random() < 0.25:
            if names and r.random() < 0.6:
                return self.pick(names)
            return self.pick(FLOAT_LITERALS)
        c = r.random()
        if c < 0.40:
            op = self.pick(["+", "-", "*", "/"])
            return "(%s %s %s)" % (self.float_expr(scope, depth - 1), op, self.float_expr(scope, depth - 1))
        if c < 0.52:
            op = self.pick(["+", "-", "*", "/"])
            left, right = self.float_expr(scope, depth - 1), self.int_expr(scope, depth - 1)
            if r.random() < 0.5:
                left, right = right, left
            return "(%s %s %s)" % (left, op, right)
        if c < 0.60:
            return "(-%s)" % self.float_expr(scope, depth - 1)
        if c < 0.70:
            inner = self.float_expr(scope, depth - 1)
            fn = self.pick(["sqrt", "sin", "cos", "exp", "sqrt"])
            if fn == "sqrt":
                return "math.sqrt(%s * %s + 1.0)" % (inner, inner)
            if fn == "exp":
                return "math.exp(math.cos(%s))" % inner
            return "math.%s(%s)" % (fn, inner)
        if c < 0.74:
            return "math.pow(%s, %s)" % (self.pick(["2.0", "1.5", "3"]), self.pick(["2", "0.5", "3"]))
        if c < 0.86:
            call = self.call_of(scope, depth, "float")
            if call:
                return call
        if c < 0.92 and scope.of("A"):
            return "%s.fa()" % self.pick(scope.of("A"))
        if c < 0.96 and self.elements(scope):
            op = self.pick(["-", "*", "/", "+"])
            a, b = self.float_expr(scope, depth - 1), self.pick(self.elements(scope))
            return "(%s %s %s)" % ((a, op, b) if r.random() < 0.5 else (b, op, a))
        return self.float_expr(scope, depth - 1)

    def string_expr(self, scope, depth):
        names = scope.of("S")
        if names and self.rng.random() < 0.5:
            return self.pick(names)
        if depth > 0 and self.rng.random() < 0.3:
            return "(%s + %s)" % (self.string_expr(scope, depth - 1), self.pick([self.int_expr(scope, 1), self.pick(STRING_LITERALS)]))
        return self.pick(STRING_LITERALS)

    def any_expr(self, scope, depth):
        c = self.rng.random()
        names = self.elements(scope)
        if c < 0.35:
            return self.int_expr(scope, depth)
        if c < 0.7:
            return self.float_expr(scope, depth)
        if c < 0.8:
            return self.string_expr(scope, depth)
        if names:
            return self.pick(names)
        return self.int_expr(scope, depth)

    def expr_of(self, kind, scope, depth, no_div=False):
        if kind == "I":
            return self.int_expr(scope, depth, no_div)
        if kind == "F":
            return self.float_expr(scope, depth)
        if kind == "S":
            return self.string_expr(scope, depth)
        return self.any_expr(scope, depth)

    def call_of(self, scope, depth, ret):
        candidates = [f for f in self.functions if f[2] == ret]
        if depth <= 0 or not candidates:
            return None
        name, params, _ = self.pick(candidates)
        args = []
        for ptype, _pname in params:
            args.append(self.argument(ptype, scope, depth - 1))
        return "%s(%s)" % (name, ", ".join(args))

    def argument(self, ptype, scope, depth):
        r = self.rng.random()
        if ptype == "integer":
            return self.int_expr(scope, depth) if r < 0.8 else (self.float_expr(scope, depth) if r < 0.92 else self.any_expr(scope, depth))
        if ptype == "float":
            return self.float_expr(scope, depth) if r < 0.75 else (self.int_expr(scope, depth) if r < 0.95 else self.any_expr(scope, depth))
        if ptype == "string":
            return self.string_expr(scope, depth) if r < 0.8 else self.any_expr(scope, depth)
        if ptype == "array<float>":
            if scope.of("L") and r < 0.6:
                return self.pick(scope.of("L"))
            return "[%s, %s, %s, 0.5]" % (self.pick(FLOAT_LITERALS), self.pick(INT_LITERALS), self.pick(FLOAT_LITERALS))
        return self.any_expr(scope, depth)

    # --------------------------------------------------------------- conditions

    def condition(self, scope, depth):
        r = self.rng.random()
        if r < 0.15:
            return "(%s && %s)" % (self.condition(scope, depth - 1), self.condition(scope, depth - 1)) if depth > 0 else "true"
        if r < 0.25:
            return "(%s || %s)" % (self.condition(scope, depth - 1), self.condition(scope, depth - 1)) if depth > 0 else "false"
        op = self.pick(["<", ">", "<=", ">=", "==", "!="])
        if r < 0.6:
            return "(%s %s %s)" % (self.int_expr(scope, depth), op, self.int_expr(scope, depth))
        if r < 0.9:
            left, right = self.float_expr(scope, depth), self.pick([self.float_expr(scope, depth), self.int_expr(scope, depth)])
            return "(%s %s %s)" % (left, op, right)
        return "(%s %s %s)" % (self.any_expr(scope, depth), op, self.any_expr(scope, depth))

    # --------------------------------------------------------------- statements

    def fresh(self, prefix):
        self.temp += 1
        return "%s%d" % (prefix, self.temp)

    def statements(self, scope, count, depth, indent, in_loop=False):
        lines = []
        for _ in range(count):
            lines.extend(self.statement(scope, depth, indent, in_loop))
        return lines

    def statement(self, scope, depth, indent, in_loop):
        r = self.rng.random()
        pad = " " * indent
        if r < 0.22:
            kind = self.pick(["I", "F", "F", "I", "O"])
            name = self.fresh("v")
            lines = [pad + "var %s = %s" % (name, self.expr_of(kind, scope, 3))]
            scope.vars[name] = kind
            return lines
        if r < 0.30:
            kind = self.pick(["I", "F"])
            name = self.fresh("t")
            typ = "integer" if kind == "I" else "float"
            # an integer expression that divides would be computed as a float
            # by the old code generator (see the module comment)
            value = self.expr_of(kind, scope, 3, no_div=True) if kind == "I" else self.expr_of("F", scope, 3)
            scope.vars[name] = kind
            return [pad + "%s %s = %s" % (typ, name, value)]
        if r < 0.45:
            names = [n for n in scope.vars if n.startswith(("v", "t", "acc"))]
            if names:
                name = self.pick(names)
                kind = scope.vars[name]
                value = self.expr_of(kind, scope, 3, no_div=(kind == "I" and name.startswith("t")))
                if name.startswith("t") and kind == "F":
                    value = self.expr_of("F", scope, 3)
                return [pad + "%s = %s" % (name, value)]
        if r < 0.58 and depth > 0:
            inner = Scope()
            inner.vars = dict(scope.vars)
            lines = [pad + "if (%s) {" % self.condition(scope, 2)]
            lines += self.statements(inner, self.rng.randint(1, 3), depth - 1, indent + 4, in_loop)
            lines.append(pad + "}")
            if self.rng.random() < 0.5:
                other = Scope()
                other.vars = dict(scope.vars)
                lines[-1] = pad + "} else {"
                lines += self.statements(other, self.rng.randint(1, 2), depth - 1, indent + 4, in_loop)
                lines.append(pad + "}")
            return lines
        if r < 0.72 and depth > 0:
            counter = self.fresh("i")
            limit = self.rng.randint(1, 5)
            inner = Scope()
            inner.vars = dict(scope.vars)
            inner.vars[counter] = "I"
            acc = self.fresh("acc")
            lines = [pad + "var %s = %s" % (counter, self.pick(["0", "0", "1"])), pad + "var %s = %s" % (acc, self.pick(["0.0", "0", "1.5"]))]
            acc_kind = "F" if "." in lines[-1] else "I"
            inner.vars[acc] = acc_kind
            scope.vars[acc] = acc_kind
            lines.append(pad + "while (%s < %d) {" % (counter, limit))
            body = self.statements(inner, self.rng.randint(1, 3), depth - 1, indent + 4, True)
            lines += body
            lines.append(pad + "    %s = %s + %s" % (acc, acc, self.expr_of(acc_kind, inner, 2, no_div=False)))
            lines.append(pad + "    %s = %s + 1" % (counter, counter))
            lines.append(pad + "}")
            lines.append(pad + "println %s" % acc)
            return lines
        if r < 0.80 and scope.of("L"):
            arr = self.pick(scope.of("L"))
            return [pad + "%s[%s] = %s" % (arr, self.pick(["0", "1", "2"]), self.pick([self.float_expr(scope, 2), self.int_expr(scope, 2)]))]
        if r < 0.86 and scope.of("A"):
            obj = self.pick(scope.of("A"))
            choice = self.rng.random()
            if choice < 0.4:
                return [pad + "%s.fa(%s)" % (obj, self.float_expr(scope, 2))]
            if choice < 0.7:
                return [pad + "%s.ia(%s)" % (obj, self.int_expr(scope, 2, no_div=False))]
            if choice < 0.85:
                return [pad + "%s.fa = %s" % (obj, self.float_expr(scope, 2))]
            return [pad + "%s.bump()" % obj]
        return [pad + "println %s" % self.expr_of(self.pick(["I", "F", "O", "S"]), scope, 3)]

    # ---------------------------------------------------------------- functions

    def function(self, index):
        r = self.rng
        name = "fn%d" % index
        params = []
        for k in range(r.randint(0, 3)):
            params.append((self.pick(PARAM_TYPES), "p%d" % k))
        ret = self.pick(["integer", "float", "float", "integer", "object", "string", "void"])
        recursive = index > 0 and r.random() < 0.25 and params and params[0][0] == "integer" and ret in ("integer", "float")
        scope = Scope()
        for ptype, pname in params:
            scope.vars[pname] = {"integer": "I", "float": "F", "object": "O", "string": "S", "array<float>": "L"}[ptype]
        if self.rng.random() < 0.4:
            scope.vars["arr"] = "L"
        lines = []
        header = "method %s(%s)%s {" % (name, ", ".join("%s %s" % p for p in params), "" if ret == "void" else ": " + ret)
        if scope.vars.get("arr"):
            lines.append("    var arr = [1, 2.5, 3, 4.5]")
        for aname in scope.of("L"):
            if aname.startswith("p"):
                lines.append("    var sum%s = 0.0" % aname)
                lines.append("    var k%s = 0" % aname)
                lines.append("    while (k%s < 3) {" % aname)
                lines.append("        sum%s = sum%s + %s[k%s] * %s - 1.5" % (aname, aname, aname, aname, self.pick(["2.0", "0.5", "3"])))
                lines.append("        %s[k%s] = sum%s" % (aname, aname, aname))
                lines.append("        k%s = k%s + 1" % (aname, aname))
                lines.append("    }")
                lines.append("    println sum%s" % aname)
                scope.vars["sum" + aname] = "F"
        if recursive:
            lines.append("    if (p0 <= 0) {")
            lines.append("        return %s" % ("1" if ret == "integer" else "0.5"))
            lines.append("    }")
        lines += self.statements(scope, r.randint(1, 4), 2, 4)
        if recursive:
            rec_args = ["p0 % 4 - 1"] + [self.argument(t, scope, 1) for t, _ in params[1:]]
            lines.append("    var rec = %s(%s)" % (name, ", ".join(rec_args)))
            scope.vars["rec"] = "I" if ret == "integer" else "F"
        for shown in list(scope.vars)[-5:]:
            if scope.vars[shown] in ("I", "F", "O", "S") and shown not in ("arr",):
                lines.append("    println %s" % shown)
        if ret != "void":
            want = {"integer": "I", "float": "F", "object": "O", "string": "S"}[ret]
            roll = r.random()
            # a number-valued method returns numbers: a string would travel on into
            # numeric fields, which the code generator trusts to hold numbers
            other = ["I", "F"] if want in ("I", "F") else ["I", "F", "O", "S"]
            if roll < 0.7:
                kind = want
            else:
                kind = self.pick(other)
            lines.append("    if (%s) {" % self.condition(scope, 1))
            lines.append("        return %s" % self.expr_of(self.pick([want, want, kind]), scope, 2))
            lines.append("    }")
            lines.append("    return %s" % self.expr_of(kind, scope, 3))
        self.functions.append((name, params, ret if ret != "void" else "void"))
        return [header] + lines + ["}"]

    # ------------------------------------------------------------------ classes

    CLASSES = """
define class A {
    private float fa: get, set
    private integer ia: get, set
    private string sa: get, set

    construct(float fa, integer ia, string sa) {
        this.fa = fa
        this.ia = ia
        this.sa = sa
    }

    method calc(): float {
        return fa * 2.0 + ia
    }

    method bump() {
        ia = ia + 1
        fa = fa + 0.5
        this.fa = this.fa * 1.5 - ia
    }

    method mix(float k): float {
        var s = 0.0
        var i = 0
        while (i < 3) {
            s = s + fa * k + ia
            i = i + 1
        }
        return s - this.fa
    }

    method sum(): integer {
        return ia + ia * 2
    }
}

define class B based A {
    private float fb: get, set

    construct(float fa, integer ia, float fb) {
        this.fa = fa
        this.ia = ia
        this.sa = "b"
        this.fb = fb
    }

    method calc(): float {
        return fa + fb * ia
    }
}

method sumFields(array<A> xs): float {
    var total = 0.0
    var i = 0
    var n = xs.length()
    while (i < n) {
        var a = xs[i]
        total = total + a.fa() * a.ia() - a.fa() / 2.0
        a.fa(a.fa() + 1.0)
        a.ia(a.ia() + 1)
        i = i + 1
    }
    return total
}

define class C {
    private integer n: get, set
    private integer m: get, set
    private float w: get, set

    construct(integer n, integer m) {
        this.n = n
        this.m = m
        this.w = 0.5
    }

    method step(): integer {
        n = n + m * 2
        m = m - 1
        return n % 7 + (n << 2) - m / 3
    }

    method ratio(): float {
        return w * n / (m + 1) + this.w
    }

    method grow(float k) {
        w = w * k
        this.n = this.n + 1
        if (w > 100.0) {
            w = w / 100.0
        }
    }
}

method sumC(array<C> cs, integer rounds): integer {
    var total = 0
    var r = 0
    while (r < rounds) {
        var i = 0
        while (i < cs.length()) {
            var c = cs[i]
            total = total + c.step() + c.n() - c.m()
            c.n(c.n() + 1)
            c.grow(1.5)
            i = i + 1
        }
        r = r + 1
    }
    return total
}

method weights(array<C> cs): float {
    var s = 0.0
    for (c in cs) {
        s = s + c.w() * c.ratio()
    }
    return s
}

method dist(A a, A b): float {
    var dx = a.fa() - b.fa()
    var dy = a.ia() - b.ia()
    return math.sqrt(dx * dx + dy * dy)
}
"""

    def program(self):
        r = self.rng
        out = ["package main", "", "import math", "",
               'method tryRun(object task): object native "nv_try_run"',
               'method tryFailed(): bool native "nv_try_failed"',
               'method tryError(): string native "nv_try_error"', "", self.CLASSES]
        count = r.randint(3, 7)
        for index in range(count):
            out.extend(self.function(index))
            out.append("")
        tasks = []
        for t in range(r.randint(3, 6)):
            scope = Scope()
            lines = []
            scope.vars["o1"] = "O"
            scope.vars["o2"] = "O"
            lines.append("        var o1 = %s" % self.pick(INT_LITERALS + FLOAT_LITERALS + STRING_LITERALS))
            lines.append("        var o2 = %s" % self.pick(INT_LITERALS + FLOAT_LITERALS))
            if r.random() < 0.7:
                scope.vars["a1"] = "A"
                scope.vars["a2"] = "A"
                lines.append("        var a1 = A(%s, %s, \"s\")" % (self.pick(FLOAT_LITERALS), self.pick(["0", "1", "5", "-3"])))
                lines.append("        var a2 = %s" % self.pick(['B(1.5, 2, 0.25)', 'A(2.5, 4, "t")', 'B(-1.0, 3, 2.0)']))
                lines.append("        var xs = [a1, a2, A(0.5, 1, \"u\")]")
                lines.append("        println sumFields(xs)")
                lines.append("        println dist(a1, a2)")
                lines.append("        println a1.calc() + a2.calc()")
                lines.append("        println a1.mix(%s) + a2.mix(2.0)" % self.pick(FLOAT_LITERALS))
                lines.append("        println a1.sum()")
            if r.random() < 0.5:
                scope.vars["arr"] = "L"
                lines.append("        var arr = [%s, %s, %s]" % (self.pick(INT_LITERALS), self.pick(FLOAT_LITERALS), self.pick(INT_LITERALS)))
            if r.random() < 0.6:
                lines.append("        var cs = [C(%s, %s), C(%s, 3)]" % (self.pick(["0", "5", "-2"]), self.pick(["1", "4"]), self.pick(["7", "100"])))
                lines.append("        println sumC(cs, %d)" % r.randint(1, 4))
                lines.append("        println weights(cs)")
            body = Generator.statements(self, scope, r.randint(2, 6), 2, 8)
            for fname, fparams, fret in self.functions:
                for _ in range(2):
                    args = ", ".join(self.argument(pt, scope, 2) for pt, _n in fparams)
                    call = "%s(%s)" % (fname, args)
                    body.append("        %s" % (call if fret == "void" else "println " + call))
            for name in list(scope.vars)[-6:]:
                if scope.vars[name] in ("I", "F", "O", "S") and name not in ("arr", "o1", "o2"):
                    body.append("        println %s" % name)
            tasks.append("define class T%d {\n    method run() {\n%s\n    }\n}\n" % (t, "\n".join(lines + body)))
        out.extend(tasks)
        main = ["method main {"]
        for t in range(len(tasks)):
            main.append("    tryRun(T%d())" % t)
            main.append("    if (tryFailed()) {")
            main.append('        println "error: " + tryError()')
            main.append("    }")
        main.append("}")
        out.extend(main)
        return "\n".join(out) + "\n"


TRAP = "error: expected a"


# The differences that are not a different result of the program, told apart
# so that none of them hides another: each one is counted by itself.
def without_nan_sign(text):
    # the sign of a NaN is whatever the processor makes of the operand order
    return text.replace("-nan", "nan")


def without_zero_sign(text):
    # a reference built by gcc 16 prints `0.0 - (double)0` as -0.000000: it folds
    # the subtraction to a negation, which is not what IEEE arithmetic says
    return text.replace("-0.000000", "0.000000")


def without_error_text(text):
    # which of two operands that both fail reports first is not specified
    return "\n".join("error:" if line.startswith("error:") else line for line in text.split("\n"))


SOFT_KINDS = (("nan sign", without_nan_sign), ("zero sign", without_zero_sign), ("error text", without_error_text))


def soft_kind(reference, candidate):
    """The one kind of difference that explains it, None when it is a real one."""
    for name, normalize in SOFT_KINDS:
        if reference[0] == candidate[0] and normalize(reference[1]) == normalize(candidate[1]):
            return name
    return None


def run_case(args):
    reference, candidate, source, work, name = args
    path = os.path.join(work, name + ".nv")
    with open(path, "w") as f:
        f.write(source)
    results = []
    for label, compiler in (("ref", reference), ("new", candidate)):
        binary = os.path.join(work, "%s.%s" % (name, label))
        env = dict(os.environ, NOVUS_CFLAGS="-O1 -w -fwrapv -ffp-contract=off")
        build = subprocess.run([compiler, "build", path, "-o", binary], capture_output=True, text=True, env=env)
        if build.returncode != 0:
            results.append(("compile", build.stdout + build.stderr))
            continue
        try:
            run = subprocess.run([binary], capture_output=True, text=True, timeout=20, stdin=subprocess.DEVNULL)
            results.append((run.returncode, run.stdout + "\n--stderr--\n" + run.stderr))
        except subprocess.TimeoutExpired:
            results.append(("timeout", ""))
    return name, source, results


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("reference")
    parser.add_argument("new")
    parser.add_argument("--count", type=int, default=200)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    parser.add_argument("--keep", default="difftyped-failures")
    parser.add_argument("--strict", action="store_true", help="count the explained differences (NaN sign, zero sign, error text) as differences")
    options = parser.parse_args()
    work = tempfile.mkdtemp(prefix="difftyped")
    cases = []
    for k in range(options.count):
        seed = options.seed * 1000003 + k
        cases.append((options.reference, options.new, Generator(random.Random(seed)).program(), work, "case%d_%d" % (options.seed, k)))
    failures = 0
    traps = 0
    soft = {}
    errors_seen = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=options.jobs) as pool:
        for name, source, results in pool.map(run_case, cases):
            if results[0][0] == "compile" and results[1][0] == "compile":
                errors_seen += 1
            if results[0] != results[1] and TRAP in results[1][1] and TRAP not in results[0][1]:
                # a value that is not what a numeric declaration promises, read by
                # typed code: reported as a type error (see README)
                traps += 1
                os.makedirs(options.keep, exist_ok=True)
                with open(os.path.join(options.keep, name + ".trap"), "w") as f:
                    f.write(source)
                continue
            kind = soft_kind(results[0], results[1]) if results[0] != results[1] else None
            if kind is not None and not options.strict:
                soft[kind] = soft.get(kind, 0) + 1
                os.makedirs(options.keep, exist_ok=True)
                with open(os.path.join(options.keep, name + ".nv"), "w") as f:
                    f.write(source)
                with open(os.path.join(options.keep, name + ".softdiff"), "w") as f:
                    f.write("%s\nREFERENCE %s\n%s\nNEW %s\n%s\n" % (kind, results[0][0], results[0][1], results[1][0], results[1][1]))
                continue
            if results[0] != results[1]:
                failures += 1
                os.makedirs(options.keep, exist_ok=True)
                with open(os.path.join(options.keep, name + ".nv"), "w") as f:
                    f.write(source)
                with open(os.path.join(options.keep, name + ".diff"), "w") as f:
                    f.write("REFERENCE %s\n%s\nNEW %s\n%s\n" % (results[0][0], results[0][1], results[1][0], results[1][1]))
                print("DIFF %s (see %s)" % (name, options.keep))
    shutil.rmtree(work, ignore_errors=True)
    explained = ", ".join("%d only in the %s" % (count, name) for name, count in sorted(soft.items())) or "none"
    print("%d programs, %d differ (more, explained: %s; %d stopped by a type error), %d did not compile in either compiler" % (options.count, failures, explained, traps, errors_seen))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
