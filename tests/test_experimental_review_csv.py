"""New review regressions, using synthetic times rather than running benchmarks."""
import csv
from pathlib import Path
import subprocess
import sys
import tempfile


def rows(path):
    with path.open(newline='', encoding='utf-8') as stream:
        return list(csv.DictReader(stream))


with tempfile.TemporaryDirectory(prefix='conway-experimental-review-') as directory:
    subprocess.run([sys.argv[1], directory], check=True)
    root = Path(directory)
    good = rows(root / 'good' / 'summary.csv')
    mpi = next(row for row in good if row['engine'] == 'mpi')
    assert mpi['requested_threads'] == '' and mpi['requested_processes'] == '2'
    assert mpi['resources_min'] == mpi['resources_max'] == '2'
    assert mpi['timing_scope'] == 'mpi_rank_max_batch'
    assert mpi['speedup'] == '0.5' and mpi['efficiency'] == '0.25'
    assert mpi['study_complete'] == mpi['cleanup_confirmed'] == mpi['valid'] == 'true'
    samples = rows(root / 'good' / 'samples.csv')
    mpi_sample = next(row for row in samples if row['engine'] == 'mpi')
    assert mpi_sample['warmups_in_session'] == '0' and mpi_sample['session_index'] == '0'
    assert mpi_sample['session_cleanup_confirmed'] == 'true'
    assert mpi_sample['load_ms'] == mpi_sample['snapshot_ms'] == '1'
    assert mpi_sample['elapsed_ms'] == '20' and mpi_sample['batch_request_ms'] == '21'
    partial = rows(root / 'partial' / 'summary.csv')
    assert all(row['study_complete'] == row['valid'] == 'false' for row in partial)
    assert all(row['diagnostic'] and row['speedup'] == row['efficiency'] == '' for row in partial)
    assert next(row for row in partial if row['engine'] == 'mpi')['speedup_status'] == 'incomplete_study'
    for case in ('bad-resources', 'bad-reference'):
        mpi = next(row for row in rows(root / case / 'summary.csv') if row['engine'] == 'mpi')
        assert mpi['speedup'] == mpi['efficiency'] == '' and mpi['valid'] == 'false'
        assert mpi['speedup_status'] == 'invalid_observations_or_reference'
    print('PASS parsed CSV v2: engine, separate resources, timing scope, session warmups and partial status')
