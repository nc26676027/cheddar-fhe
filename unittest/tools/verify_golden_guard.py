"""Prove that an imaginary-sign error after the sampled slots fails validation."""
import gzip
from pathlib import Path
import struct
import subprocess
import sys

repo = Path(__file__).resolve().parents[2]
build = repo / (sys.argv[1] if len(sys.argv) > 1 else "build")
logs = repo / ".scratch/phase1-acceptance/logs"
fixture = build / "unittest/meboot_golden_4434.bin.gz"
original = fixture.read_bytes()
data = bytearray(gzip.decompress(original))
slots, _ = struct.unpack_from("<II", data, 8)
offset = struct.unpack_from("<I", data, 20)[0]
for slot in range(2, slots):
    position = 24 + slot * 16 + 8  # first diagonal's imaginary component
    value = struct.unpack_from("<d", data, position)[0]
    if abs(value) > 1e-6:
        struct.pack_into("<d", data, position, -value)
        break
else:
    raise RuntimeError("No later nonzero imaginary coefficient found")

try:
    fixture.write_bytes(gzip.compress(data, mtime=0))
    command = [
        str(build / "unittest/meboot_matrix_planner_test"),
        "--gtest_filter=*Testbed32.MEBootMatrixPlannerCtSGeneration*",
        f"--gtest_output=xml:{logs / 'golden-mutation.xml'}",
    ]
    result = subprocess.run(command, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, check=False)
    (logs / "golden-mutation.log").write_text(
        f"Mutated stage 0 offset {offset} slot {slot}: imaginary sign\n"
        f"Command: {command!r}\n{result.stdout}", encoding="utf-8")
    if result.returncode != 1 or "[  FAILED  ]" not in result.stdout:
        raise RuntimeError(f"Mutation did not trigger a test failure: {result.returncode}")
    print(f"Golden guard passed: sign corruption at slot {slot} was rejected")
finally:
    fixture.write_bytes(original)
