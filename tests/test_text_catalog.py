"""Line interface: candidate playback cannot implicitly commit or use MPI."""
import os
from pathlib import Path
import subprocess
import sys
sys.dont_write_bytecode=True

def check(binary,root):
    commands='M\nM\nR\n2\nD\nC\nM\npatterns/blinker.txt\nn\nu\nB\n\nC\nM\npatterns/block.txt\nn\nx\nx\nC\nM\n/no/such/file\nx\n2\nQ\n'
    result=subprocess.run([binary],input=commands,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,cwd=root,
        env=dict(os.environ,OMP_NUM_THREADS='2'),timeout=15)
    out=result.stdout
    assert result.returncode==0,out
    assert 'Didáctico requiere una terminal interactiva con FTXUI' in out,out
    assert 'Vista previa: Secuencial · sin confirmar · generación 1' in out,out
    assert 'Patrón confirmado: patterns/blinker.txt' in out,out
    assert 'Patrón confirmado: patterns/block.txt' not in out,out
    assert 'Error al cargar:' in out and 'se conserva el patrón confirmado' in out,out
    assert 'Estados equivalentes: SI' in out,out
    assert 'MODOS PRINCIPALES' in out and 'CONFIGURACIÓN' in out,out
    assert 'No se pudo completar la sesión MPI' not in out,out
    assert 'MPI | procesos' not in out,out
    print('PASS line catalogue, validated initial confirmation, cancellation/invalid path, MPI resource selection and FTXUI notice')
if __name__=='__main__':check(str(Path(sys.argv[1]).resolve()),str(Path(sys.argv[2]).resolve()))
