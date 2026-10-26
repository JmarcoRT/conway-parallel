"""Test-only launcher: reject any previous worker belonging to this test process."""
import os
from pathlib import Path
import sys

parent = os.getppid()
records = {}
for path in Path('/proc').iterdir():
    if path.name.isdecimal():
        try:
            fields = (path / 'stat').read_text().rsplit(')', 1)[1].split()
            records[int(path.name)] = (int(fields[1]), (path / 'comm').read_text().strip())
        except (OSError, ValueError):
            pass
for pid, (_, name) in records.items():
    if not name.startswith('conway_mpi_work'):
        continue
    ancestor = pid
    for _ in range(128):
        if ancestor == parent:
            sys.exit('A previous MPI worker is still alive before the next turn')
        ancestor = records.get(ancestor, (0, ''))[0]
        if not ancestor:
            break
os.execv(sys.argv[1], sys.argv[1:])
