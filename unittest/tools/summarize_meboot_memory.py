"""Observed device-memory peaks during individual bootstrap test cases."""
import csv
from datetime import datetime, timedelta
import json
from pathlib import Path
import sys
import xml.etree.ElementTree as ET

evidence = Path(sys.argv[1])
samples = []
with (evidence / "gpu-telemetry.csv").open() as handle:
    for row in csv.DictReader(handle):
        fields = {key.strip(): value.strip() for key, value in row.items()}
        samples.append((datetime.strptime(fields["timestamp"], "%Y/%m/%d %H:%M:%S.%f"),
                        int(fields["memory.used [MiB]"].split()[0])))
report = []
for name in ("boot_test", "meboot_performance_test"):
    for case in ET.parse(evidence / f"{name}.xml").getroot().iter("testcase"):
        if case.find("skipped") is not None:
            continue
        start = datetime.fromisoformat(case.get("timestamp"))
        end = start + timedelta(seconds=float(case.get("time")))
        values = [used for timestamp, used in samples if start <= timestamp <= end]
        if values:
            report.append({"test": case.get("classname") + "." + case.get("name"),
                           "observed_device_peak_mib": max(values), "samples": len(values),
                           "full_case_time_covered": samples[0][0] <= start and samples[-1][0] >= end})
output = {"sampling_interval_ms": 500,
          "measurement": "whole-device memory.used, includes driver/background processes and allocator reserve; observed sampled peaks, not exact allocation maxima",
          "cases": report}
(evidence / "memory-summary.json").write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")
print(json.dumps(output, indent=2))
