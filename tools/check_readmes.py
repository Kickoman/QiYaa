#!/usr/bin/env python3
"""Runs the checks the READMEs write down and checks that every module is documented.

    python3 tools/check_readmes.py      # from anywhere; needs bash and GNU grep

A check is a command in a ```bash block of src/*/README.md or tests/README.md whose line holds
"# must print nothing"; a line ending in a backslash goes on into the next. Each check runs from
the repository root, and any output, on stdout or stderr, fails it. Every folder of src/ must also
have a README.md and a row in the module table of docs/architecture.md.
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MARK = "# must print nothing"


def checks(readme: Path):
    """Yields (line number, command) for each check in the README's bash blocks."""
    language = None  # the open block's language, None outside a block
    start, command = 0, ""
    for number, line in enumerate(readme.read_text(encoding="utf-8").splitlines(), 1):
        stripped = line.strip()
        if stripped.startswith("```"):
            language = stripped[3:] if language is None else None
            command = ""
            continue
        if language != "bash":
            continue
        if not command:
            start = number
        if stripped.endswith("\\"):
            command += stripped[:-1] + " "
            continue
        command += stripped
        if MARK in command:
            yield start, command
        command = ""


def run(command: str) -> str:
    result = subprocess.run(["bash", "-c", command], cwd=ROOT, capture_output=True, text=True)
    return result.stdout + result.stderr


def module_problems() -> list[str]:
    table = (ROOT / "docs" / "architecture.md").read_text(encoding="utf-8")
    listed = set(re.findall(r"^\| \[src/(\w+)\]", table, re.MULTILINE))
    problems = []
    for folder in sorted(p for p in (ROOT / "src").iterdir() if p.is_dir()):
        if not (folder / "README.md").is_file():
            problems.append(f"src/{folder.name} has no README.md")
        if folder.name not in listed:
            problems.append(f"src/{folder.name} is not in the module table of docs/architecture.md")
    return problems


def main() -> int:
    readmes = sorted(ROOT.glob("src/*/README.md")) + [ROOT / "tests" / "README.md"]
    failed = 0
    count = 0
    for readme in readmes:
        for number, command in checks(readme):
            count += 1
            output = run(command)
            if output:
                failed += 1
                print(f"{readme.relative_to(ROOT)}:{number}: {command}")
                print("".join(f"    {line}\n" for line in output.splitlines()))
    for problem in module_problems():
        failed += 1
        print(problem)
    print(f"{count} README checks, {failed} problems")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
