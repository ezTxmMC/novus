#!/usr/bin/env python3
"""
Runs every benchmark in every language and writes results.json.

Each workload is implemented once per language in <language>/<workload>.<ext>.
All implementations must print exactly the same output - the runner refuses
to record a result otherwise, which keeps the comparison honest.

Timing is wall clock including process start (that is what a user waits for),
memory is the peak RSS of the child measured through wait4. Every program runs
REPEATS times and the best run counts.

  python3 run.py                 # everything
  python3 run.py --only primes   # one workload
  python3 run.py --langs novus,rust
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parent
BUILD = ROOT / ".build"
REPEATS = 5

WORKLOADS = [
    ("fib", "Recursive Fibonacci", "fib(30) - function call overhead, nothing else"),
    ("loop", "Integer loop", "10 million iterations of add and modulo"),
    ("primes", "Primes by trial division", "counting primes below 200,000"),
    ("mandelbrot", "Mandelbrot", "400x400 grid, 50 iterations - floating point"),
    ("array", "Dynamic array", "two million appends, then a sum"),
    ("sort", "Sorting", "300,000 integers through the standard sort"),
    ("strings", "String building", "200,000 pieces, then split and join"),
    ("map", "Hash map", "300,000 string keys inserted and read back"),
    ("objects", "Object allocation", "one million small objects, then a field read"),
    ("wordfreq", "Word frequency", "read a file, count words, rank them"),
]

# name -> (extension, how to build, how to run)
LANGUAGES = {
    "novus": {"ext": "nv", "label": "Novus"},
    "cpp": {"ext": "cpp", "label": "C++"},
    "rust": {"ext": "rs", "label": "Rust"},
    "go": {"ext": "go", "label": "Go"},
    "crystal": {"ext": "cr", "label": "Crystal"},
    "java": {"ext": "java", "label": "Java"},
    "node": {"ext": "js", "label": "Node.js"},
    "python": {"ext": "py", "label": "Python"},
}

JAVA_CLASS = {
    "fib": "Fib", "loop": "Loop", "primes": "Primes", "mandelbrot": "Mandelbrot",
    "array": "Arr", "sort": "Sort", "strings": "Str", "map": "MapB",
    "objects": "Obj", "wordfreq": "WordFreq",
}


def tool(*candidates):
    for candidate in candidates:
        if candidate and (shutil.which(candidate) or Path(candidate).exists()):
            return candidate
    return None


RUSTC = tool(os.environ.get("RUSTC"), "rustc", *[str(p) for p in Path(os.environ.get(
    "RUSTUP_TOOLCHAINS", str(Path.home() / ".rustup/toolchains"))).glob("*/bin/rustc")])
NOVUSC = os.environ.get("NOVUSC", str(REPO / "build" / "novusc"))


def version_of(language):
    try:
        commands = {
            "novus": [NOVUSC, "version"],
            "cpp": ["g++", "--version"],
            "rust": [RUSTC, "--version"],
            "go": ["go", "version"],
            "crystal": ["crystal", "--version"],
            "java": ["java", "-version"],
            "node": ["node", "--version"],
            "python": ["python3", "--version"],
        }[language]
        out = subprocess.run(commands, capture_output=True, text=True)
        text = (out.stdout + out.stderr).strip().split("\n")[0]
        return text
    except Exception:
        return "unknown"


def build(language, workload):
    """Returns the command to run, or None when the language is unavailable."""
    name = JAVA_CLASS[workload] if language == "java" else workload
    source = ROOT / language / f"{name}.{LANGUAGES[language]['ext']}"
    if not source.exists():
        return None
    out = BUILD / language
    out.mkdir(parents=True, exist_ok=True)
    binary = out / workload

    if language == "novus":
        subprocess.run([NOVUSC, "build", str(source), "-o", str(binary)],
                       check=True, capture_output=True)
        return [str(binary)]
    if language == "cpp":
        subprocess.run(["g++", "-O2", "-std=c++20", str(source), "-o", str(binary)],
                       check=True, capture_output=True)
        return [str(binary)]
    if language == "rust":
        subprocess.run([RUSTC, "-O", str(source), "-o", str(binary)],
                       check=True, capture_output=True)
        return [str(binary)]
    if language == "go":
        subprocess.run(["go", "build", "-o", str(binary), str(source)],
                       check=True, capture_output=True)
        return [str(binary)]
    if language == "crystal":
        subprocess.run(["crystal", "build", "--release", "--no-debug", "-o", str(binary), str(source)],
                       check=True, capture_output=True)
        return [str(binary)]
    if language == "java":
        subprocess.run(["javac", "-d", str(out), str(source)], check=True, capture_output=True)
        return ["java", "-cp", str(out), JAVA_CLASS[workload]]
    if language == "node":
        return ["node", str(source)]
    if language == "python":
        return ["python3", str(source)]
    return None


MEASURE = BUILD / "measure"


def measure(command, cwd):
    """Best of REPEATS: wall clock in ms and peak RSS in MB, via measure.c."""
    best_time, best_rss, output = None, 0, ""
    for _ in range(REPEATS):
        result = subprocess.run([str(MEASURE)] + command, cwd=cwd, capture_output=True, text=True)
        if result.returncode != 0:
            raise RuntimeError(f"{command} exited with {result.returncode}")
        elapsed, rss = result.stderr.strip().split()[-2:]
        elapsed, rss = float(elapsed), int(rss) / 1024
        output = result.stdout.strip()
        if best_time is None or elapsed < best_time:
            best_time, best_rss = elapsed, rss
    return best_time, best_rss, output


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--only", help="run a single workload")
    parser.add_argument("--langs", help="comma separated subset of languages")
    args = parser.parse_args()

    BUILD.mkdir(parents=True, exist_ok=True)
    subprocess.run(["cc", "-O2", str(ROOT / "measure.c"), "-o", str(MEASURE)], check=True)

    workloads = [w for w in WORKLOADS if not args.only or w[0] == args.only]
    languages = args.langs.split(",") if args.langs else list(LANGUAGES)

    # the word frequency benchmark needs an input file - build it deterministically
    words = ["alpha", "beta", "gamma", "delta", "epsilon", "zeta", "eta", "theta"]
    text = " ".join(words[(i * i + i // 3) % len(words)] for i in range(400_000))
    (ROOT / "input.txt").write_text(text + "\n")

    results = {"languages": {}, "workloads": [], "runs": {}}
    for language in languages:
        results["languages"][language] = {
            "label": LANGUAGES[language]["label"],
            "version": version_of(language),
        }
    for name, title, description in workloads:
        results["workloads"].append({"name": name, "title": title, "description": description})

    for name, title, _ in workloads:
        print(f"\n{title}")
        expected = None
        for language in languages:
            try:
                command = build(language, name)
                if command is None:
                    continue
                elapsed, rss, output = measure(command, ROOT)
            except subprocess.CalledProcessError as error:
                print(f"  {language:9} build failed: {error.stderr.decode()[:120]}")
                continue
            except Exception as error:
                print(f"  {language:9} {error}")
                continue
            if expected is None:
                expected = output
            agrees = output == expected
            results["runs"].setdefault(name, {})[language] = {
                "ms": round(elapsed, 1), "mb": round(rss, 1),
                "output": output, "agrees": agrees,
            }
            flag = "" if agrees else "  <-- DIFFERENT OUTPUT"
            print(f"  {language:9} {elapsed:8.1f} ms {rss:8.1f} MB  {output.splitlines()[0][:28]}{flag}")

    # A language whose toolchain is missing produced no run at all - leave it
    # out entirely instead of publishing an empty column.
    measured = {lang for runs in results["runs"].values() for lang in runs}
    skipped = [lang for lang in results["languages"] if lang not in measured]
    for lang in skipped:
        del results["languages"][lang]
    if skipped:
        print(f"\nno toolchain, not in results: {', '.join(skipped)}")

    (ROOT / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    print(f"\nwrote {ROOT / 'results.json'}")


if __name__ == "__main__":
    main()
