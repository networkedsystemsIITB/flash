import re
import subprocess
from sys import argv

BENCH_PATH = "examples/unit-tests/ring-benchmark"


def run_benchmark() -> (float, float):
    result = subprocess.run(
        [f"./build/{BENCH_PATH}"], stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )
    output = result.stdout.decode("utf-8")

    owner_time = re.search(r"owner dequeue: ([\d.]+) ns", output).group(1)
    guest_time = re.search(r"guest enqueue: ([\d.]+) ns", output).group(1)

    return float(owner_time), float(guest_time)


def run_benchmarks(times_heat: int, times_run: int) -> (list[float], list[float]):
    for _ in range(times_heat):
        run_benchmark()

    owner_lt = []
    guest_lt = []

    for _ in range(times_run):
        owner_time, guest_time = run_benchmark()
        owner_lt.append(owner_time)
        guest_lt.append(guest_time)

    return owner_lt, guest_lt


def run_sed(old: str, new: str):
    if (
        subprocess.run(
            [
                "sed",
                "-i",
                f"s|{old}|{new}|g",
                BENCH_PATH + ".c",
            ]
        ).returncode
        != 0
    ):
        print("error running sed")
        exit(1)


def enable_bp():
    return run_sed("// #define BP", "#define BP")


def disable_bp():
    return run_sed("#define BP", "// #define BP")


def enable_mpsc():
    return run_sed("// #define MPSC", "#define MPSC")


def disable_mpsc():
    return run_sed("#define MPSC", "// #define MPSC")


def make():
    if (
        subprocess.run(
            ["make"], stdout=subprocess.PIPE, stderr=subprocess.PIPE
        ).returncode
        != 0
    ):
        print("error running make")
        exit(1)


if __name__ == "__main__":
    if len(argv) != 3:
        print("Usage: python ring-benchmark.py <times_heat> <times_run>")
        exit(1)

    times_heat = int(argv[1])
    times_run = int(argv[2])

    disable_bp()
    disable_mpsc()
    make()

    print("SPSC - 2 Threads")
    owner_lt, guest_lt = run_benchmarks(times_heat, times_run)
    print(f"owner dequeue: {sum(owner_lt) / len(owner_lt)} ns")
    print(f"guest enqueue: {sum(guest_lt) / len(guest_lt)} ns")

    enable_bp()
    make()

    print("\nSPSC - 1 Thread")
    owner_lt, guest_lt = run_benchmarks(times_heat, times_run)
    print(f"owner dequeue: {sum(owner_lt) / len(owner_lt)} ns")
    print(f"guest enqueue: {sum(guest_lt) / len(guest_lt)} ns")

    disable_bp()
    enable_mpsc()
    make()

    print("\nMPSC - 2 Threads")
    owner_lt, guest_lt = run_benchmarks(times_heat, times_run)
    print(f"owner dequeue: {sum(owner_lt) / len(owner_lt)} ns")
    print(f"guest enqueue: {sum(guest_lt) / len(guest_lt)} ns")

    enable_bp()
    make()

    print("\nMPSC - 1 Thread")
    owner_lt, guest_lt = run_benchmarks(times_heat, times_run)
    print(f"owner dequeue: {sum(owner_lt) / len(owner_lt)} ns")
    print(f"guest enqueue: {sum(guest_lt) / len(guest_lt)} ns")
