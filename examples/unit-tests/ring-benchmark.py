import itertools
import re
import subprocess
from sys import argv

BENCH_FILE = "examples/unit-tests/ring-benchmark"
DEFS = {key: 0 for key in ["MPSC", "BATCHING", "BP"]}

with open(f"./{BENCH_FILE}.c", "r") as fh:
    lines = fh.readlines()


def find_indices():
    for key in DEFS:
        def_str = f"#define {key}"
        idx = next((i for i, line in enumerate(lines) if def_str in line), None)
        if idx is None:
            print(f"error: {def_str} not found in {BENCH_FILE}.c")
            exit(1)

        DEFS[key] = idx


find_indices()


def make():
    result = subprocess.run(["make"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode != 0:
        print("error running make")
        exit(1)


def configure(config: dict[str, bool]):
    for key in DEFS:
        lines[DEFS[key]] = (
            f"#define {key}\n" if config.get(key, False) else f"// #define {key}\n"
        )

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

    print("\n" + "-" * 40)
    for comb in itertools.product([False, True], repeat=len(DEFS)):
        configure(dict(zip(DEFS.keys(), comb)))
        print("MPSC: {}, BATCHING: {}, BP: {}".format(*comb))
        print_benchmarks(times_heat, times_run)
        print("-" * 40)

    configure({})
