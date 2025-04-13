import re
import subprocess
from sys import argv

BENCH_FILE = "examples/unit-tests/ring-benchmark"

with open(f"./{BENCH_FILE}.c", "r") as fh:
    for i, line in enumerate(fh.readlines()):
        if "#define MPSC" in line:
            mpsc_idx = i
        if "#define BP" in line:
            bp_idx = i


def make():
    result = subprocess.run(["make"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode != 0:
        print("error running make")
        exit(1)


def configure(mpsc: bool, bp: bool):
    with open(f"./{BENCH_FILE}.c", "r") as fh:
        lines = fh.readlines()

    lines[mpsc_idx] = "#define MPSC\n" if mpsc else "// #define MPSC\n"
    lines[bp_idx] = "#define BP\n" if bp else "// #define BP\n"

    with open(f"./{BENCH_FILE}.c", "w") as fh:
        fh.writelines(lines)

    make()


def run_benchmark() -> (float, float):
    result = subprocess.run(
        [f"./build/{BENCH_FILE}"], stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )
    output = result.stdout.decode("utf-8")

    enqueue_time = re.search(r"enqueue: ([\d.]+) ns", output).group(1)
    dequeue_time = re.search(r"dequeue: ([\d.]+) ns", output).group(1)

    return float(enqueue_time), float(dequeue_time)


def print_benchmarks(times_heat: int, times_run: int):
    for _ in range(times_heat):
        run_benchmark()

    enqueue_lt = []
    dequeue_lt = []

    for _ in range(times_run):
        enqueue_time, dequeue_time = run_benchmark()
        enqueue_lt.append(enqueue_time)
        dequeue_lt.append(dequeue_time)

    print(f"enqueue: {sum(enqueue_lt) / len(enqueue_lt)} ns")
    print(f"dequeue: {sum(dequeue_lt) / len(dequeue_lt)} ns")


if __name__ == "__main__":
    if len(argv) != 3:
        print("Usage: python ring-benchmark.py <times_heat> <times_run>")
        exit(1)

    times_heat = int(argv[1])
    times_run = int(argv[2])

    configure(False, False)
    print("SPSC - 2 Threads")
    print_benchmarks(times_heat, times_run)

    configure(False, True)
    print("\nSPSC - 1 Threads")
    print_benchmarks(times_heat, times_run)

    configure(True, False)
    print("\nMPSC - 2 Threads")
    print_benchmarks(times_heat, times_run)

    configure(True, True)
    print("\nMPSC - 1 Thread")
    print_benchmarks(times_heat, times_run)

    configure(False, False)
