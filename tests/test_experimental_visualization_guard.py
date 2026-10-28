"""Live didactic MPI ownership still guards both experimental executors."""
from pathlib import Path
import sys
sys.dont_write_bytecode=True
from test_ftxui_didactic_controller import mpi_guard
if __name__=='__main__':
    mpi_guard(*(str(Path(p).resolve()) for p in sys.argv[1:]))
