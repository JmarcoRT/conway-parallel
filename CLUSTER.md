# Cluster de Conway Parallel

## 1. Dependencias

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake git python3 curl \
  libfabric-dev libfabric1 \
  libpsm-infinipath1 libpsm2-2 \
  librdmacm1 libibverbs1 \
  libnl-3-200 libnl-route-3-200 libnuma1
```

## 2. MPICH 4.2.3 con ch4:ofi

No usar el paquete `mpich` de Ubuntu.

```bash
cd /tmp
curl -L -o mpich.tar.gz \
  https://www.mpich.org/static/downloads/4.2.3/mpich-4.2.3.tar.gz
tar xzf mpich.tar.gz
cd mpich-4.2.3

./configure \
  --prefix=/opt/mpich \
  --disable-fortran \
  --disable-shared \
  --enable-fast=O2 \
  --with-device=ch4:ofi

make -j"$(nproc)"
sudo make install
```

## 3. ZeroTier

```bash
curl -s https://install.zerotier.com | sudo bash
sudo zerotier-cli join 154a350c8604d619
```

En el panel de ZeroTier: **Member Devices -> Authorize All**.

## 4. Clonar y compilar

```bash
git clone -b feat/add-cluster \
  https://github.com/JmarcoRT/conway-parallel.git conway-parallel

cd conway-parallel
```

```bash
cmake -S . -B build-mpich \
  -DBUILD_TESTING=OFF \
  -DMPI_CXX_COMPILER=/opt/mpich/bin/mpicxx \
  -DMPI_C_COMPILER=/opt/mpich/bin/mpicc

cmake --build build-mpich --target conway_cluster -j"$(nproc)"
```

## 5. Servidor

```bash
cd conway-parallel/scripts/cluster
python3 cluster.py init
```

Responder:

```text
Rol: servidor
Network ID: 154a350c8604d619
Token de ZeroTier: Enter
Nodos: 4
IP del servidor en ZeroTier: <IP zt del servidor>
Patron: <ruta a patterns/glider.txt>
Pasos: 20
```

Arrancar:

```bash
python3 cluster.py server
```

Firewall:

```bash
sudo ufw allow in on zt+
```

## 6. Clientes

En cada cliente:

```bash
git clone -b feat/add-cluster \
  https://github.com/JmarcoRT/conway-parallel.git conway-parallel

cd conway-parallel

cmake -S . -B build-mpich \
  -DBUILD_TESTING=OFF \
  -DMPI_CXX_COMPILER=/opt/mpich/bin/mpicxx \
  -DMPI_C_COMPILER=/opt/mpich/bin/mpicc

cmake --build build-mpich --target conway_cluster -j"$(nproc)"

cd scripts/cluster
python3 cluster.py init
```

Responder:

```text
Rol: cliente
Network ID: 154a350c8604d619
Nodo: 1
Nodos: 4
IP del servidor en ZeroTier: <IP zt del servidor>
Patron: <ruta a patterns/glider.txt>
Pasos: 20
```

Arrancar:

```bash
python3 cluster.py client
```

## 7. Resultados

El servidor escribe:

```text
~/.config/conway/runtime/metrics.csv
~/.config/conway/runtime/board_final.txt
```

```text
PASS conway-cluster pattern=... P=4 steps=20 global_alive=4
```
