# Cliente y trabajador MPI independientes (Linux/WSL local)

Esta etapa añade `conway_mpi_worker` y la biblioteca `conway_mpi_client`.
No cambia `Game`, menús, simulación interactiva, modo didáctico ni experimentos.
Reutiliza `core::step_mpi`, la partición por filas, halos y doble buffer existentes.

## Uso y propiedad

`mpi::Client` recibe `ClientOptions` con rutas del lanzador/trabajador, P y plazos
de inicio, operación y cierre. Su constructor crea un directorio privado 0700,
un socket Unix y un grupo de procesos mediante `posix_spawnp`. Lanza:

```text
mpirun [argumentos explícitos del llamador] --host localhost -np P conway_mpi_worker
       --socket /tmp/conway-mpi-XXXXXX/control --session ID
```

P cuenta procesos de cálculo, incluido rank 0, sin incluir al cliente/lanzador.
Se valida P en 1..1024 como límite operativo de esta primera versión; no es una
recomendación de CPU. No se añade sobresuscripción ni se calcula una recomendación.
El runtime puede rechazar P por slots/binding disponibles. Cliente y lanzador
deben ser compatibles con el MPI enlazado al trabajador.

La API síncrona `load`, `advance`, `snapshot`, `close` está pensada para un hilo
de aplicación dedicado, no para renderizado. `try_lock` rechaza peticiones
simultáneas. La destrucción requiere que ningún otro hilo esté usando el objeto.
El destructor intenta cierre ordenado; `close` explícito permite observar errores.
El cliente es el único propietario de la recogida del lanzador: no debe existir
otro `waitpid`/manejador SIGCHLD que recoja ese mismo hijo concurrentemente.

Solo rank 0 usa IPC; difunde las órdenes y reparte el tablero con Scatterv.
Todos los ranks calculan; los vacíos participan en las colectivas sin halos.
Las llamadas MPI ocurren en el único hilo de aplicación de cada trabajador,
inicializado con `MPI_THREAD_SINGLE`. El cliente no hace llamadas MPI.

## Contrato v1

Cabecera de 48 bytes: seis enteros unsigned de 64 bits, big endian:
magic `0x434f4e5741594d50`, versión 1, tipo, sesión, petición, longitud.
Los enteros del payload usan el mismo formato. Las células son bytes 0/1 row-major.
No se serializan estructuras nativas. Lectura/escritura contemplan fragmentación,
EINTR, EAGAIN y EOF; cada operación comparte un plazo absoluto entre envío y respuesta.
El inicio incluye preparación, lanzamiento y handshake; LOAD incluye preparación
del payload; CLOSE comparte el mismo plazo entre CLOSED y salida del lanzador.
Cada fragmento conserva ese plazo. La escalada de limpieza tiene su intervalo
adicional acotado de gracia, seguido de recogida del hijo por el SO.

Antes de reservar un payload se validan magic, versión, tipo, sesión, petición
y longitud según el contexto. LOAD y snapshots leen primero 24/32 bytes fijos
de metadatos y comprueban dimensiones, conteos y longitud antes de reservar el
tablero. Células y estado se validan antes de aplicar la orden; una carga inválida
no sustituye el tablero. El trabajador admite espera ociosa sin plazo, pero desde
el primer byte aplica un plazo único de 30 segundos al frame y respuesta; las
colectivas MPI siguen dependiendo del timeout/supervisión externa del cliente.

| Mensaje | Payload / respuesta |
|---|---|
| READY (1) | P y PID de cada rank; petición 0, sesión del lanzamiento |
| LOAD (2) | ancho, alto, generación y células; REPLY vacío tras carga en todos los ranks |
| ADVANCE (3) | cantidad 1..100000; REPLY vacío tras completar todos los pasos |
| SNAPSHOT (4) | vacío; REPLY con ancho, alto, generación, población global y células |
| CLOSE (5) | vacío; CLOSED después de MPI_Finalize |
| REPLY (6), CLOSED (7), ERROR (8) | misma sesión/petición; ERROR contiene texto |

LOAD permite reemplazar la entrada y conservar una generación no nula. SNAPSHOT
no avanza. No hay reproducción autónoma: pausa significa no enviar avances.
El cliente valida sesión, petición y tipo de respuesta. Rank 0 exige identificadores
de petición crecientes. Errores remotos o de transporte invalidan la sesión cliente;
no hay reintentos ambiguos de ADVANCE ni recuperación del último estado confirmado.

Se limita cada payload a 64 MiB; el tablero a 64 MiB menos 32 bytes para el
snapshot, además de las validaciones existentes de dimensiones y conteos int.
Este límite no es un presupuesto agregado de RAM: cliente/rank 0 pueden conservar
copias completas durante carga y snapshot.
64 MiB es una política conservadora para esta primera conexión, no un límite de
MPI: evita reservas arbitrariamente grandes por una longitud recibida y acota
los buffers completos. Restringe tableros que sí cabrían en RAM/conteos MPI.
Un único límite compartido es más sencillo que introducir streaming o negociación
de presupuestos en esta etapa; podrá cambiarse de forma coordinada en ambos extremos.

## Supervisión y límites

stdout/stderr del runtime van a un pipe separado, drenado durante las esperas
con hasta 32 KiB retenidos para diagnóstico. Los NUL de PMIx se omiten del texto.
El socket privado, la sesión aleatoria y SO_PEERCRED verifican el peer local.

Ante error o timeout, se desconecta el socket, se envían señales al grupo del
lanzador y a los ranks identificados mediante pidfds, se concede un intervalo
de salida y se escala a SIGKILL; se recoge el hijo y se eliminan socket/directorio.
El PID del lanzador se mantiene sin recoger hasta terminar las señales para
evitar reutilización del identificador de grupo. Los trabajadores usan PDEATHSIG
como respaldo si desaparece su padre de runtime durante el arranque.
Antes de aceptar pidfds, el cliente verifica el peer, PIDs únicos y su ascendencia
respecto de su propio lanzador en `/proc`; solo incorpora handles de procesos
vivos verificados. No señaliza PIDs anunciados sin verificar. Si el lanzador ya
terminó, sus handles siguen identificando a los ranks; las pruebas incluyen su
muerte temprana y una segunda sesión que debe seguir funcionando. Tras limpiar,
se borran las rutas guardadas para no tocar nombres reutilizados en otro cierre.

Los pidfds no son una exigencia de MPI: son la decisión de supervisión de esta
implementación. Evitan señalizar un proceso ajeno si un rank muere y se reutiliza
su PID, incluso tras terminar el lanzador. Añaden un descriptor por rank y dos
syscalls Linux, pero evitan un fallback racy basado en comprobar `/proc` y luego
enviar señales por PID. La ascendencia verifica propiedad; el pidfd fija identidad.
Un cgroup dedicado sería otra opción, con requisitos de permisos/delegación que
no asumimos. Si pidfds no están disponibles se falla limpiamente, sin degradar
la garantía de identidad. Esto excluye kernels antiguos o entornos que bloqueen
estas syscalls, aunque tengan MPI.

Requiere Linux con pidfd_open (kernel 5.3+) y WSL2 equivalente, y un runtime MPI
local. No soporta MPI multinodo, reanudación tras muerte de rank, cancelación
interna de una colectiva ni lanzamiento desde un proceso ya inicializado con MPI.
Los timeouts recuperan el control descartando todo el trabajo, no continuándolo.
La limpieza presupone que el sistema operativo puede terminar y recoger procesos;
no garantiza recuperación de procesos bloqueados dentro del kernel.

## Validación

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --target mpi_client_tests -j2
ctest --test-dir build -R '^mpi_client$' --output-on-failure
```

La prueba tiene timeout CTest de 45 segundos y habilita `--oversubscribe`
explícitamente como política exclusiva de pruebas. Comprueba P=1,2,4; carga de
blinker, ocho pasos con equivalencia completa frente al secuencial, recarga con
generación inicial 12 y ranks sin filas, cierre idempotente y ausencia de PIDs,
grupo, socket y directorio residuales. Incluye lanzador/trabajador inexistentes,
lanzador ruidoso sin handshake, respuesta con ID erróneo, muerte de rank 0 y
rank 0 detenido (timeout). También prueba frames fragmentados, límite de payload y EOF.

| Pruebas de fallo | Qué ejecutan |
|---|---|
| Muerte/desconexión de rank 0, SIGSTOP/timeout, muerte del lanzador con otra sesión activa | Lanzador y trabajador MPI reales |
| Tipos/sesión/metadatos inválidos, carga con células inválidas y estado conservado | Peer de prueba enviando bytes al trabajador MPI real |
| Destructor, doble cierre y nombres de recursos reutilizados | Cliente con trabajador MPI real |
| Launcher/worker inexistentes | Fallo de exec; no llega a ejecutar un trabajador |
| Lanzador ruidoso que no conecta, respuesta con ID erróneo, PID ajeno anunciado | Dobles de prueba, sin MPI |
| Lectura por goteo, escritura parcial con lector lento, payload excesivo, EOF | Socketpair local; sin MPI |

Las pruebas por goteo y escritura parcial deben agotar un plazo absoluto aunque
haya progreso. El doble que anuncia un PID ajeno comprueba que ese proceso sigue
vivo; los fallos reales comprueban ausencia de PIDs/grupos/socket/directorio residuales.
