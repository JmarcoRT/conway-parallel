"""Parse actual GPU experiment exports, including references and CUDA-only fields."""
import csv
import math
import os
from pathlib import Path
import subprocess
import sys
import tempfile

def rows(path):
    with path.open(encoding='utf-8',newline='') as stream:
        result=list(csv.DictReader(stream))
    assert result and all(None not in row and all(v is not None for v in row.values()) for row in result),result
    return result

with tempfile.TemporaryDirectory(prefix='conway-cuda-csv-') as directory:
    run=subprocess.run(sys.argv[1:],env=dict(os.environ,CONWAY_CUDA_EXPORT_DIR=directory),capture_output=True,text=True)
    if run.returncode==77:
        print(run.stdout);sys.exit(77)
    assert run.returncode==0,(run.stdout,run.stderr)
    for name in ('comparison','blocks'):
        root=Path(directory)/name
        samples=rows(root/'samples.csv');summary=rows(root/'summary.csv');meta=rows(root/'metadata.csv')
        assert all(row['schema_version']=='3' for row in samples+summary)
        metadata={row['key']:row['value'] for row in meta}
        assert metadata['csv.schema_version']=='3' and metadata['protocol.version']=='conway-experimental-v3'
        assert 'nvcc' in metadata['build.command.cuda'],metadata.get('build.command.cuda')
        assert any(key.endswith('.device_name') and value for key,value in metadata.items())
        for row in samples:
            reference=samples[int(row['sequential_reference_sample'])]
            assert reference['engine']=='sequential' and reference['round']==row['round']
            assert row['valid']==row['equivalent']==row['cleanup_confirmed']=='true'
            if row['engine']=='cuda':
                assert row['timing_scope']=='cuda_resident_batch_wall'
                for key in ('requested_threads','requested_processes','effective_threads_min','effective_threads_max','observed_processes','session_index'):
                    assert row[key]=='',(key,row)
                for key in ('cuda_device_ms','cuda_prepare_ms','cuda_snapshot_ms','cuda_total_operation_ms','elapsed_ms'):
                    assert math.isfinite(float(row[key])) and float(row[key])>=0
                assert float(row['cuda_total_operation_ms'])>=float(row['elapsed_ms'])
                assert row['cuda_cleanup_confirmed']=='true'
                x,y=int(row['cuda_block_x']),int(row['cuda_block_y'])
                assert (x,y) in ((8,8),(16,16),(32,8))
                assert int(row['cuda_grid_x'])==(17+x-1)//x and int(row['cuda_grid_y'])==(17+y-1)//y
            else:
                assert row['cuda_block_x']==row['cuda_block_y']==row['cuda_device_ms']==''
        reference=next(row for row in summary if row['engine']=='sequential')
        for row in summary:
            if row['engine']=='cuda':
                assert row['efficiency']=='' and row['efficiency_status']=='not_applicable_cuda'
                expected=float(reference['median_ms'])/float(row['median_ms'])
                assert math.isclose(float(row['speedup']),expected,rel_tol=1e-12)
        if name=='blocks':assert {row['configuration_id'] for row in summary if row['engine']=='cuda'}=={'cuda:8x8','cuda:16x16','cuda:32x8'}
print('PASS parsed CUDA CSV v3, device metadata, timing scopes, block grids, references and metrics')
