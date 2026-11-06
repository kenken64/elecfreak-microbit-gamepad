"""Build and run the host-side tests in tests/ (no micro:bit needed).

    python tools/run_tests.py              # all tests
    python tools/run_tests.py --keep       # keep the built binaries

Needs a host C compiler: cc, gcc or clang. The header is plain C, so the same
test also compiles as C++ (the V1 firmware is C++).
"""
import argparse
import pathlib
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
TESTS = ROOT / "tests"
COMPILERS = ["cc", "gcc", "clang", "tcc"]
CPP_COMPILERS = ["c++", "g++", "clang++"]
FLAGS = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-I" + str(ROOT / "src")]
CPP_FLAGS = ["-x", "c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
             "-I" + str(ROOT / "src")]


def find(names):
    for name in names:
        found = shutil.which(name)
        if found:
            return found
    return None


def build_and_run(compiler, flags, source, binary, label):
    build = subprocess.run(
        [compiler, *flags, str(source), "-o", str(binary)],
        capture_output=True, text=True, encoding="utf-8", errors="replace",
    )
    if build.returncode != 0:
        print("FAIL %s (%s) did not compile" % (source.relative_to(ROOT), label))
        print(build.stderr.strip())
        return False

    run = subprocess.run([str(binary)], capture_output=True, text=True,
                         encoding="utf-8", errors="replace")
    if run.stdout.strip():
        print("[%s] %s" % (label, run.stdout.strip()))
    if run.returncode != 0:
        print(run.stderr.strip())
        return False
    return True


def main():
    parser = argparse.ArgumentParser(description="Build and run the host tests")
    parser.add_argument("--compiler", help="C compiler to use (default: auto-detect)")
    parser.add_argument("--keep", action="store_true", help="keep the built binaries")
    args = parser.parse_args()

    compiler = args.compiler or find(COMPILERS)
    if not compiler:
        sys.exit("No C compiler found (%s). Install one, or pass --compiler PATH."
                 % ", ".join(COMPILERS))
    cpp_compiler = find(CPP_COMPILERS)

    sources = sorted(TESTS.glob("test_*.c"))
    if not sources:
        sys.exit("No tests found in tests/")

    failed = []
    out_dir = pathlib.Path(tempfile.mkdtemp(prefix="gp-tests-"))
    suffix = ".exe" if sys.platform == "win32" else ""

    print("compiler:", compiler)
    if cpp_compiler:
        print("C++ check:", cpp_compiler)

    for source in sources:
        print("--- %s" % source.relative_to(ROOT))
        if not build_and_run(compiler, FLAGS, source,
                             out_dir / (source.stem + suffix), "C"):
            failed.append(source.name)
            continue

        # The V1 firmware is C++, so the same header must also build there.
        if cpp_compiler and not build_and_run(cpp_compiler, CPP_FLAGS, source,
                                              out_dir / (source.stem + "-cpp" + suffix),
                                              "C++"):
            failed.append(source.name + " (C++)")

    if not args.keep:
        shutil.rmtree(out_dir, ignore_errors=True)
    else:
        print("binaries kept in", out_dir)

    if failed:
        sys.exit("\n%d test run(s) failed: %s" % (len(failed), ", ".join(failed)))

    print("\nAll tests passed.")


if __name__ == "__main__":
    main()
