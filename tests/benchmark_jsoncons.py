#!/usr/bin/env python3
"""Measure JSONCONS SAX, an optional preserved DOM binary, and available YAJL.

Example:
  python3 tests/benchmark_jsoncons.py --baseline /path/to/old/jsonbench \
      --binary src/jsonbench --output /tmp/jsoncons-benchmark.json

The synthetic workload is a root array of short, three-field objects. It is
intended for a reproducible local comparison, not a ModSecurity performance
claim. The original jsoncons DOM adapter performs no argument/path/limit work.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shlex
import signal
import statistics
import subprocess
import sys
import tempfile
import time


REPOSITORY = Path(__file__).resolve().parent.parent
ITEM = b'{"name":"abcdefghijklmnopqrstuvwxyz0123456789","id":123456789,"active":true}'
UINT_MAX = 4294967295


def positive_int(text):
    value = int(text)
    if value <= 0:
        raise argparse.ArgumentTypeError("must be a positive integer")
    return value


def size_bytes(text):
    match = re.fullmatch(r"([0-9]+)(B|K|KB|KiB|M|MB|MiB|G|GB|GiB)?", text, re.IGNORECASE)
    if not match:
        raise argparse.ArgumentTypeError("use bytes or a size such as 400KiB, 5MiB, or 32MiB")
    suffix = (match.group(2) or "B").upper()
    power = {"B": 0, "K": 1, "KB": 1, "KIB": 1,
             "M": 2, "MB": 2, "MIB": 2, "G": 3, "GB": 3, "GIB": 3}[suffix]
    value = int(match.group(1)) * 1024 ** power
    if value < len(ITEM) + 2:
        raise argparse.ArgumentTypeError("size must fit at least one benchmark object")
    if ((value - 1) // (len(ITEM) + 1)) * 3 > UINT_MAX:
        raise argparse.ArgumentTypeError("generated argument count would exceed the CLI limit")
    return value


def file_hash(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def executable(parser, path, label):
    path = path.expanduser().resolve()
    if not path.is_file() or not os.access(path, os.X_OK):
        hint = ("preserve the original executable before rebuilding, then pass --baseline PATH"
                if label == "baseline" else "run ./autogen.sh, ./configure, and make first")
        parser.error(f"{label} executable is missing or not executable: {path}; {hint}")
    return path


def available_engines(binary):
    result = subprocess.run([str(binary), "-h"], stdin=subprocess.DEVNULL,
                            capture_output=True, text=True, check=False)
    if result.returncode:
        raise RuntimeError(f"{binary} -h failed: {result.stderr.strip()}")
    return set(re.findall(r"^\s*- ([A-Z][A-Z0-9]+)\s*$", result.stdout, re.MULTILINE))


def configured_buffer(path):
    if not path.is_file():
        raise RuntimeError(f"configured header missing: {path}; run ./configure or pass --config PATH")
    text = path.read_text()
    match = re.search(r"^\s*#\s*define\s+FILE_BUFFER_SIZE\s+([0-9]+)[uUlL]*\s*$", text, re.MULTILINE)
    if not match or int(match.group(1)) < 2:
        raise RuntimeError(f"cannot determine FILE_BUFFER_SIZE from {path}")
    return int(match.group(1))


def dataset(directory, target):
    count = (target - 1) // (len(ITEM) + 1)
    path = directory / f"input-{target}.json"
    digest = hashlib.sha256()
    with path.open("wb") as stream:
        def write(block):
            stream.write(block)
            digest.update(block)

        write(b"[" + ITEM)
        remaining = count - 1
        block = (b"," + ITEM) * 4096
        while remaining >= 4096:
            write(block)
            remaining -= 4096
        write((b"," + ITEM) * remaining)
        write(b"]")
    return path, {"target_bytes": target, "bytes": path.stat().st_size,
                  "objects": count, "arguments": count * 3, "sha256": digest.hexdigest()}


def invoke(helper, binary, engine, path, argument_count, timeout):
    command = [str(helper), str(binary), "-s", "-a", str(argument_count),
               "-n", "1", "-e", engine, str(path)]
    start = time.perf_counter()
    process = subprocess.Popen(command, stdin=subprocess.DEVNULL,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               start_new_session=True)
    try:
        stdout, stderr = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        stdout, stderr = process.communicate()
        return {"status": "failed", "reason": f"timeout after {timeout} seconds",
                "exit_code": process.returncode,
                "stdout": stdout.decode(errors="replace"), "stderr": stderr.decode(errors="replace")}
    except BaseException:
        # Ctrl-C must also stop the helper and its parser subprocess.
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.communicate()
        raise
    wall = time.perf_counter() - start
    stdout = stdout.decode(errors="replace")
    stderr = stderr.decode(errors="replace")
    timing = re.search(r"Time:\s+([0-9.eE+-]+)\s+sec", stdout)
    usage = re.search(r"^JSONBENCH_RUSAGE (\{.*\})$", stderr, re.MULTILINE)
    if process.returncode or not timing or not usage:
        return {"status": "failed", "exit_code": process.returncode,
                "reason": "parser failed or required timing/resource output is missing",
                "stdout": stdout, "stderr": stderr}
    result = {"status": "ok", "parser_seconds": float(timing.group(1)), "wall_seconds": wall}
    result.update(json.loads(usage.group(1)))
    diagnostics = re.sub(r"^JSONBENCH_RUSAGE .*\n?", "", stderr, flags=re.MULTILINE).strip()
    if diagnostics:
        result["stderr"] = diagnostics
    return result


def summary(samples):
    result = {}
    for metric in ("parser_seconds", "wall_seconds", "peak_rss_kib"):
        values = [sample[metric] for sample in samples]
        result[metric] = {"mean": statistics.mean(values),
                          "sample_stdev": statistics.stdev(values) if len(values) > 1 else 0,
                          "min": min(values), "max": max(values)}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary", type=Path, default=REPOSITORY / "src/jsonbench")
    parser.add_argument("--baseline", type=Path, help="preserved executable containing the original DOM implementation")
    parser.add_argument("--config", type=Path, default=REPOSITORY / "config.h",
                        help="configured header used to build --binary, for YAJL's file buffer limit")
    parser.add_argument("--samples", type=positive_int, default=5)
    parser.add_argument("--warmups", type=positive_int, default=1)
    parser.add_argument("--sizes", type=size_bytes, nargs="+", default=None)
    parser.add_argument("--timeout", type=positive_int, default=60, help="seconds per process")
    parser.add_argument("--output", type=Path, help="write JSON report here instead of stdout")
    args = parser.parse_args()
    if platform.system() != "Linux":
        parser.error("the peak-RSS helper currently requires Linux (ru_maxrss is reported in KiB)")

    binary = executable(parser, args.binary, "SAX")
    baseline = executable(parser, args.baseline, "baseline") if args.baseline else None
    config = args.config.expanduser().resolve()
    try:
        file_buffer_size = configured_buffer(config)
        engines = available_engines(binary)
        if "JSONCONS" not in engines:
            raise RuntimeError(f"JSONCONS is unavailable in {binary}; configure with jsoncons headers installed")
        if baseline and "JSONCONS" not in available_engines(baseline):
            raise RuntimeError(f"JSONCONS is unavailable in the baseline executable {baseline}")
    except (OSError, RuntimeError) as error:
        parser.error(str(error))

    common_size = min(400 * 1024, file_buffer_size - 1)
    sizes = args.sizes or [max(common_size, len(ITEM) + 2), 5 * 1024 * 1024, 32 * 1024 * 1024]
    compiler = shlex.split(os.environ.get("CC", "cc"))
    if not compiler:
        parser.error("CC is empty; set CC to a C compiler or leave it unset to use cc")

    artifacts = {"binary": {"path": str(binary), "sha256": file_hash(binary)},
                 "config": {"path": str(config), "sha256": file_hash(config)}}
    if baseline:
        artifacts["baseline"] = {"path": str(baseline), "sha256": file_hash(baseline)}
    report = {"schema_version": 1, "platform": platform.platform(), "artifacts": artifacts,
              "method": {"samples": args.samples, "warmups": args.warmups,
                         "cli": "-s -a GENERATED_ARGUMENT_COUNT -n 1 -e ENGINE INPUT",
                         "input": "root array of identical objects: short ASCII string, exact integer, boolean",
                         "timing": "CLI parser elapsed seconds; independent fresh processes after warmups",
                         "rss": "Linux wait4 child peak RSS in KiB via a small compiled C launcher",
                         "file_buffer_size": file_buffer_size,
                         "yajl_maximum_file_bytes": file_buffer_size - 1,
                         "compiler": compiler,
                         "caveat": "Original DOM adapter only parses/discards a DOM. SAX also builds argument paths and enforces argument/size limits. Synthetic cached-file workload; no ModSecurity performance conclusion."},
              "inputs": [], "runs": []}
    failed = False
    with tempfile.TemporaryDirectory(prefix="jsonbench-benchmark-") as temp:
        directory = Path(temp)
        helper = directory / "benchmark-rusage"
        compile_command = compiler + ["-O2", "-std=c11", "-Wall", "-Wextra",
                                      str(Path(__file__).with_name("benchmark_rusage.c")), "-o", str(helper)]
        try:
            compile_result = subprocess.run(compile_command, capture_output=True, text=True, check=False)
        except OSError as error:
            parser.error(f"cannot run C compiler {compiler[0]}: {error}")
        if compile_result.returncode:
            parser.error("cannot compile peak-RSS helper: " + compile_result.stderr.strip())

        for target in sizes:
            path, info = dataset(directory, target)
            report["inputs"].append(info)
            variants = [("jsoncons_sax", binary, "JSONCONS")]
            if baseline:
                variants.insert(0, ("jsoncons_dom", baseline, "JSONCONS"))
            else:
                report["runs"].append({"variant": "jsoncons_dom", "input_bytes": info["bytes"],
                                       "status": "skipped", "reason": "no baseline supplied; pass --baseline /path/to/preserved/executable"})
            if "YAJL" not in engines:
                report["runs"].append({"variant": "yajl", "input_bytes": info["bytes"],
                                       "status": "skipped", "reason": "YAJL is unavailable in the configured binary"})
            elif info["bytes"] > file_buffer_size - 1:
                report["runs"].append({"variant": "yajl", "input_bytes": info["bytes"],
                                       "status": "skipped", "reason": f"input exceeds configured YAJL file limit of {file_buffer_size - 1} bytes"})
            else:
                variants.append(("yajl", binary, "YAJL"))

            for name, target_binary, engine in variants:
                run = {"variant": name, "input_bytes": info["bytes"], "status": "ok", "warmups": [], "samples": []}
                for _ in range(args.warmups):
                    sample = invoke(helper, target_binary, engine, path, info["arguments"], args.timeout)
                    run["warmups"].append(sample)
                    if sample["status"] != "ok":
                        run["status"] = "failed"
                        break
                if run["status"] == "ok":
                    for _ in range(args.samples):
                        sample = invoke(helper, target_binary, engine, path, info["arguments"], args.timeout)
                        run["samples"].append(sample)
                        if sample["status"] != "ok":
                            run["status"] = "failed"
                            break
                if run["status"] == "ok":
                    run["summary"] = summary(run["samples"])
                    metrics = run["summary"]
                    print(f"{name}: {info['bytes']} bytes, "
                          f"{metrics['parser_seconds']['mean'] * 1000:.3f} +/- "
                          f"{metrics['parser_seconds']['sample_stdev'] * 1000:.3f} ms, "
                          f"{metrics['peak_rss_kib']['mean']:.0f} KiB mean peak RSS", file=sys.stderr)
                else:
                    failed = True
                    print(f"FAILED {name} for {info['bytes']} bytes: inspect JSON report diagnostics", file=sys.stderr)
                report["runs"].append(run)

    for label, artifact in artifacts.items():
        if file_hash(Path(artifact["path"])) != artifact["sha256"]:
            failed = True
            report.setdefault("errors", []).append(f"{label} changed during measurement; results are invalid")
    report["status"] = "failed" if failed else "ok"
    output = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.write_text(output)
        print(f"JSON report: {args.output}", file=sys.stderr)
    else:
        sys.stdout.write(output)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
