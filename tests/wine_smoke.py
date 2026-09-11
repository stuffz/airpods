import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def main():
    executable = Path(sys.argv[1]).resolve(strict=True)
    failures = []
    with tempfile.TemporaryDirectory(prefix="airpods-wine-") as temporary:
        directory = Path(temporary)
        environment = dict(
            os.environ,
            WINEPREFIX=str(directory / "prefix"),
            WINEDEBUG="fixme-all",
            WINEDLLOVERRIDES="mscoree,mshtml=",
        )
        try:
            boot = run_wine(environment, "wineboot", "--init")
            if boot.returncode != 0:
                raise RuntimeError("Wine prefix initialization failed")
            version = run_wine(environment, str(executable), "--version")
            check(
                version.returncode == 0
                and re.search(r"built \d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}", version.stdout),
                "version contains a build timestamp",
                failures,
            )
            for name in ("keys with spaces.conf", "räksmörgås.conf", "電池.conf"):
                fixture = directory / name
                fixture.write_text(
                    "IRK 000102030405060708090a0b0c0d0e0f\n"
                    "ENC_KEY 0f0e0d0c0b0a09080706050403020100\n",
                    encoding="ascii",
                )
                result = run_wine(
                    environment,
                    str(executable),
                    "--ble",
                    "--key-file",
                    "Z:" + str(fixture).replace("/", "\\"),
                )
                check(
                    "Loaded proximity keys" in result.stdout,
                    f"key import: {name}",
                    failures,
                )
        finally:
            stopped = subprocess.run(
                ["wineserver", "-k"], env=environment, capture_output=True, timeout=10, check=False
            )
            # Wine returns 1 without diagnostics when the server has already exited.
            if stopped.returncode != 1 or stopped.stderr:
                stopped.check_returncode()
            subprocess.run(["wineserver", "-w"], env=environment, check=True, timeout=10)
    return 1 if failures else 0


def run_wine(environment, *arguments):
    # Wine services inherit stdout; a pipe can stay open after the command exits.
    with tempfile.TemporaryFile(mode="w+", encoding="utf-8", errors="replace") as output:
        result = subprocess.run(
            ["wine", *arguments],
            env=environment,
            stdout=output,
            stderr=subprocess.STDOUT,
            timeout=30,
            check=False,
        )
        output.seek(0)
        result.stdout = output.read()
        return result


def check(passed, name, failures):
    print(f"{'PASS' if passed else 'FAIL'}: {name}", flush=True)
    if not passed:
        failures.append(name)


if __name__ == "__main__":
    sys.exit(main())
