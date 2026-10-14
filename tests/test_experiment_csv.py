"""Independent CSV reader: only Python standard library, existing test dependency."""
import csv
import json
import math
import os
from pathlib import Path
import statistics
import subprocess
import sys
import tempfile


def read(path):
    with path.open(encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream)
        rows = list(reader)
        assert all(None not in row and None not in row.values() for row in rows)
        return reader.fieldnames, rows


def load(directory):
    samples_header, samples = read(directory / "samples.csv")
    summary_header, summary = read(directory / "summary.csv")
    metadata_header, metadata = read(directory / "metadata.csv")
    assert samples_header == ["schema_version", "study_id", "experiment_type", "configuration_id", "engine", "requested_threads",
        "effective_threads_min", "effective_threads_max", "repetition", "round", "position", "execution_order",
        "sequential_reference_sample", "generations", "elapsed_ms", "elapsed_ms_status", "equivalent", "valid", "invalid_reason"]
    assert summary_header == ["schema_version", "study_id", "experiment_type", "configuration_id", "engine", "requested_threads",
        "effective_threads_min", "effective_threads_max", "sample_count", "min_ms", "max_ms", "mean_ms", "median_ms",
        "stddev_population_ms", "statistics_status", "speedup", "speedup_status", "efficiency", "efficiency_status",
        "equivalent", "valid", "invalid_reason"]
    assert metadata_header == ["study_id", "key", "value", "source", "detail"]
    assert len({r["study_id"] for r in samples + summary + metadata}) == 1
    assert len({r["key"] for r in metadata}) == len(metadata)
    return samples, summary, {r["key"]: r for r in metadata}


with tempfile.TemporaryDirectory(prefix="conway-csv-roundtrip-") as temporary:
    root = Path(temporary)
    subprocess.run([sys.argv[1], sys.argv[2], "--fixture", str(root)], check=True)
    samples, summary, metadata = load(root / "comparison")
    assert len(samples) == 4 and len(summary) == 2
    expected = [1.2345678901234567, 0.12345678901234566, 0.33333333333333331, 1.0]
    assert [float(r["elapsed_ms"]) for r in samples] == expected
    assert [r["engine"] for r in samples] == ["sequential", "openmp", "openmp", "sequential"]
    assert [int(r["execution_order"]) for r in samples] == [1, 2, 3, 4]
    assert [int(r["sequential_reference_sample"]) for r in samples] == [0, 0, 3, 3]
    assert samples[0]["study_id"] == 'comparación,"A"\nα'
    assert metadata["input.pattern_name"]["value"] == 'patrón,"uno"\nsegunda línea'
    assert metadata["user.note"]["value"] == 'synthetic fixture, "quoted"\r\nsegunda línea ñ'
    assert metadata["input.pattern_name"]["source"] == "user"
    assert metadata["input.seed"]["value"] == "" and metadata["input.seed"]["source"] == "unavailable"
    assert metadata["openmp.runtime_version"]["source"] == "unavailable"
    assert metadata["execution.context_captured_at_utc"]["value"].endswith("Z")
    assert metadata["build.command.sequential"]["source"] == "build_captured"
    assert "-std=c++20" in metadata["build.command.sequential"]["value"]
    assert "-fopenmp" in metadata["build.command.openmp"]["value"]
    with (Path(sys.argv[1]).resolve().parent / "compile_commands.json").open(encoding="utf-8") as stream:
        commands = json.load(stream)
    for name, suffix in {"sequential": "src/core/engines/sequential.cpp", "openmp": "src/core/engines/openmp.cpp",
                         "benchmark": "src/app/benchmark.cpp", "comparison": "src/app/experiment_runner.cpp",
                         "scaling": "src/app/scaling_study.cpp"}.items():
        expected_commands = [entry["command"] for entry in commands
                             if entry["file"].endswith(suffix) and "CMakeFiles/conway_experiments.dir/" in entry["command"]]
        assert metadata["build.command." + name]["value"] in expected_commands
    for row in summary:
        times = [float(r["elapsed_ms"]) for r in samples if r["configuration_id"] == row["configuration_id"]]
        assert int(row["sample_count"]) == len(times)
        assert float(row["min_ms"]) == min(times) and float(row["max_ms"]) == max(times)
        assert float(row["median_ms"]) == statistics.median(times)
        assert math.isclose(float(row["mean_ms"]), statistics.mean(times), rel_tol=1e-15)
        assert math.isclose(float(row["stddev_population_ms"]), statistics.pstdev(times), rel_tol=1e-15)
    assert summary[0]["speedup"] == "" and summary[0]["speedup_status"] == "not_applicable"
    assert float(summary[1]["speedup"]) == float(summary[0]["median_ms"]) / float(summary[1]["median_ms"])
    samples, summary, metadata = load(root / "scaling")
    assert len(samples) == 6 and len(summary) == 3
    assert sum(r["engine"] == "sequential" for r in samples) == 2
    assert [r["configuration_id"] for r in samples] == ["sequential", "openmp:1", "openmp:3", "openmp:1", "openmp:3", "sequential"]
    assert [int(r["sequential_reference_sample"]) for r in samples] == [0, 0, 0, 5, 5, 5]
    assert metadata["input.seed"]["value"] == "0" and metadata["input.origin"]["value"] == "generator"
    assert metadata["input.width"]["value"] == "5" and metadata["input.height"]["value"] == "3"
    assert len(metadata["input.fingerprint"]["value"]) == 16
    assert metadata["execution.requested_threads"]["value"] == "1;3"
    assert metadata["input.requested_density_per_10000"]["value"] == "3000"
    assert summary[1]["speedup"] == "" and summary[1]["speedup_status"] == "non_equivalent"
    assert summary[1]["valid"] == "false" and samples[1]["invalid_reason"] == "non_equivalent"
    assert summary[2]["speedup_status"] == "available" and summary[2]["efficiency"] == ""
    assert summary[2]["efficiency_status"] == "variable_effective_threads"
    samples, summary, metadata = load(root / "zero")
    assert all(float(r["elapsed_ms"]) == 0 for r in samples)
    assert summary[1]["speedup"] == "" and summary[1]["speedup_status"] == "insufficient_clock_resolution"
    samples, summary, metadata = load(root / "invalid")
    assert samples[1]["elapsed_ms"] == "nan" and samples[1]["valid"] == "false"
    assert summary[1]["median_ms"] == "" and summary[1]["speedup"] == ""
    assert summary[1]["statistics_status"] == "unavailable_in_result"
    # A normal user cannot create a new directory below a read-only parent.
    if hasattr(os, "geteuid") and os.geteuid() != 0:
        protected = root / "protected"
        protected.mkdir(mode=0o500)
        try:
            blocked = subprocess.run([sys.argv[1], sys.argv[2], "--fixture", str(protected)],
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            assert blocked.returncode != 0 and not list(protected.iterdir())
        finally:
            protected.chmod(0o700)
print("PASS: independent UTF-8 CSV round-trip, rows, precision, provenance, invalid values and permissions")
