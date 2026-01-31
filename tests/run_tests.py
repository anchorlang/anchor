import argparse
import subprocess
import sys
from pathlib import Path


def parse_directive(path):
    with open(path) as f:
        first_line = f.readline().strip()
    if first_line.startswith("# expect_error:"):
        return ("expect_error", first_line[len("# expect_error:") :].strip())
    if first_line.startswith("# expect:"):
        return ("expect", int(first_line[len("# expect:") :].strip()))
    return None


def run_test(ancc, path):
    directive = parse_directive(path)
    if not directive:
        return "SKIP", f"no directive in {path.name}"

    kind = directive[0]
    result = subprocess.run(
        [ancc, "run", str(path)], capture_output=True, text=True, timeout=30
    )

    if kind == "expect":
        expected_code = directive[1]
        if result.returncode != expected_code:
            return (
                "FAIL",
                f"expected exit code {expected_code}, got {result.returncode}",
            )
        return "PASS", None

    if kind == "expect_error":
        expected_msg = str(directive[1])
        if result.returncode == 0:
            return "FAIL", "expected error but build succeeded"
        if expected_msg not in result.stderr:
            return (
                "FAIL",
                f"expected '{expected_msg}' in stderr, got: {result.stderr.strip()}",
            )
        return "PASS", None

    return "SKIP", f"unknown directive '{kind}' in {path.name}"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--filter", default="")
    parser.add_argument(
        "--ancc", default=str(Path(__file__).parent.parent / "bin" / "ancc.exe")
    )
    args = parser.parse_args()

    cases_dir = Path(__file__).parent / "cases"
    tests = sorted(cases_dir.glob("*.anc"))
    if args.filter:
        tests = [t for t in tests if args.filter in t.name]

    passed = failed = skipped = 0
    for test in tests:
        status, msg = run_test(args.ancc, test)
        name = test.stem
        if status == "PASS":
            print(f"  PASS  {name}")
            passed += 1
        elif status == "SKIP":
            print(f"  SKIP  {name}: {msg}")
            skipped += 1
        else:
            print(f"  FAIL  {name}: {msg}")
            failed += 1

    print(f"\n{passed} passed, {failed} failed, {skipped} skipped")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
