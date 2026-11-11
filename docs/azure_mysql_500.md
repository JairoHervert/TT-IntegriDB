# Evaluación con Azure Database for MySQL: 500 alumnos

Este despliegue utiliza **únicamente el escenario de 500 alumnos** (15,171
registros académicos). No modifica las consultas C1–C14, el esquema
criptográfico ni los estados utilizados en VirtualBox. El almacenamiento SQL
pasa de un contenedor MySQL a **Azure Database for MySQL Flexible Server**.

## Arquitectura y alcance

| Componente | Ubicación | Observaciones |
|---|---|---|
| Azure Database for MySQL Flexible Server | Servicio administrado de Azure | MySQL 8.4, TLS y acceso privado |
| `tt_prover` | VM Azure `vm-tt-prover` | Conecta por FQDN y puerto 3306 al servicio administrado |
| `tt_verifier` y frontend | VM Azure `vm-tt-verifier` | Solo se comunica con el probador por IP privada |
| `tt_owner` y clave secreta | VM original del propietario, fuera de Azure | No transferir `keys/secret_key.txt` |

El servicio MySQL y el probador pertenecen al **dominio no confiable** del
modelo. El verificador es el componente confiable y debe permanecer en una
VM distinta. La conexión cifrada TLS autentica al servicio de base de datos,
pero **no sustituye** la verificación de las respuestas `(R, pi)`.

```
[Propietario original, no Azure]
  claves privadas, Init y Setup previos para 500
  ├─ deploy/prover/   --> VM Azure probador (estado publicable)
  └─ deploy/verifier/ --> VM Azure verificador (digest y clave pública)

[VNet Azure]
  VM probador  -- TCP 3306/TLS --> Azure Database for MySQL Flexible Server
      |
      +-- TCP 7070 (red privada) --> VM verificador --> frontend :8080
                                                    (túnel SSH al navegador)
```

**No repetir `Init`, `Setup` ni `Publish`** si se reutilizan el conjunto de
datos exacto y los archivos publicables de 500 alumnos. Sí se debe verificar
la equivalencia luego de la importación; una discrepancia de datos invalidaría
las pruebas. Si no se conservan los archivos del estado autenticado, habrá
que generar otro estado completo desde el propietario, no reconstruirlo en
el servidor no confiable.

## 1. Recursos que se crean en Azure

El administrador de la suscripción debe confirmar los precios y el saldo de
crédito **antes** de crear recursos. Las máquinas virtuales, los discos, las
IP públicas y Azure Database for MySQL pueden facturarse por separado.

- Un grupo de recursos, por ejemplo `rg-tt-azure500`.
- Una VNet, por ejemplo `vnet-tt`, en una sola región.
- Una subred para VM y otra subred delegada **exclusivamente** a
  `Microsoft.DBforMySQL/flexibleServers`.
- Azure Database for MySQL **Flexible Server**, versión **8.4** si está
  disponible en la región, sin alta disponibilidad para esta prueba corta.
  Elegir acceso **Private access (VNet Integration)** y habilitar DNS privado.
- Dos máquinas **Ubuntu Server 24.04 LTS**, x64, en la subred de VM:
  `vm-tt-prover` y `vm-tt-verifier`.
- Reglas NSG: 3306 hacia MySQL desde la VM probador; TCP 7070 hacia
  la VM probador solo desde la VM verificador; SSH 22 solo desde la IP del
  responsable. No abrir 3306, 7070 ni 8080 a Internet.

Para resultados de desempeño comparables entre ejecuciones, preferir núcleos
no sujetos a créditos de CPU; una VM serie B sirve para revisar el
funcionamiento pero puede introducir variación temporal. Registrar el SKU
de las VM, SKU del servicio MySQL, región, versión exacta del motor,
almacenamiento y topología de red. **No asumir equivalencia de hardware**
con VirtualBox.

Azure proporciona un **nombre de host completo (FQDN)**, por ejemplo
`<nombre-servidor>.mysql.database.azure.com`. Usar el nombre real mostrado
en el portal: no reemplazarlo por una IP privada, pues se requiere para
verificar el certificado del servidor.

## 2. Preparación de cada VM Azure

En **ambas VM**, una vez que SSH funcione:

```bash
sudo apt update
sudo apt install -y docker.io docker-compose-v2 git ca-certificates
sudo systemctl enable --now docker
sudo usermod -aG docker "$USER"
# Cerrar la sesión SSH y volver a entrar para usar el grupo docker.
git clone URL_DEL_REPOSITORIO TT-IntegriDB
cd ~/TT-IntegriDB
```

Usar el **mismo commit** en ambas VM, preferentemente el commit etiquetado
para la evaluación. Las imágenes se compilan con el mismo código del
prototipo local; `compose.azure.prover.yml` y `compose.azure.verifier.yml`
son las únicas configuraciones de despliegue nuevas.

## 3. Cargar la base de 500 alumnos en MySQL administrado

Ejecutar desde la **VM probador**, que debe poder resolver el DNS privado.
Se utiliza el cliente `mysql` solamente para inicializar el servicio
administrado; el probador C++ después accede directamente por TCP/TLS.

```bash
sudo apt install -y mysql-client
cd ~/TT-IntegriDB
read -r -p 'FQDN MySQL de Azure: ' TT_DB_HOST
read -r -p 'Usuario administrador MySQL de Azure: ' TT_AZURE_ADMIN
getent hosts "$TT_DB_HOST"
```

**En estos comandos `-p` solicita la contraseña de forma interactiva**.
Se usa un paquete de CA raíz del sistema, con verificación de hostname.

```bash
mysql --host="$TT_DB_HOST" --port=3306 --user="$TT_AZURE_ADMIN" -p \
  --ssl-mode=VERIFY_IDENTITY --ssl-ca=/etc/ssl/certs/ca-certificates.crt \
  -e 'CREATE DATABASE IF NOT EXISTS academic_db CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;'

mysql --host="$TT_DB_HOST" --port=3306 --user="$TT_AZURE_ADMIN" -p \
  --ssl-mode=VERIFY_IDENTITY --ssl-ca=/etc/ssl/certs/ca-certificates.crt \
  academic_db < academic_records_db/init/01_schema.sql

mysql --host="$TT_DB_HOST" --port=3306 --user="$TT_AZURE_ADMIN" -p \
  --ssl-mode=VERIFY_IDENTITY --ssl-ca=/etc/ssl/certs/ca-certificates.crt \
  academic_db < academic_records_db/init/02_seed_data.sql
```

**ADVERTENCIA:** `01_schema.sql` elimina y recrea las cuatro tablas del
prototipo. Importar solo en una base `academic_db` nueva, dedicada a estas
pruebas. No hacerlo en un servidor compartido con otras aplicaciones.

Comprobar los datos **antes de conectar el probador**:

```bash
mysql --host="$TT_DB_HOST" --port=3306 --user="$TT_AZURE_ADMIN" -p \
  --ssl-mode=VERIFY_IDENTITY --ssl-ca=/etc/ssl/certs/ca-certificates.crt \
  academic_db -e 'SELECT COUNT(*) AS alumnos FROM student; SELECT COUNT(*) AS registros FROM academic_record; SELECT grade FROM academic_record WHERE record_id=14; SHOW STATUS LIKE "Ssl_cipher";'
```

Valores esperados: **500 alumnos, 15,171 registros académicos y calificación 6
para `record_id=14`**. `Ssl_cipher` no debe estar vacío. Comprobar también
la versión del motor con `SELECT VERSION()`.

Crear un usuario de **solo lectura** para el probador. Desde una consola
MySQL conectada como administrador, reemplazando la contraseña de ejemplo
por una distinta y robusta, ejecutar:

```sql
CREATE USER 'tt_reader'@'%' IDENTIFIED BY 'CAMBIAR_POR_CLAVE_SEGURA';
GRANT SELECT ON academic_db.* TO 'tt_reader'@'%';
```

No copiar la contraseña a Git, a scripts versionados ni al reporte.
El usuario administrador se utiliza únicamente para cargar datos y, si se
requiere, para las pruebas controladas de alteración. El probador solo debe
recibir permisos de lectura.

## 4. Publicar los archivos criptográficos de 500 alumnos

Desde la **VM original del propietario**, copiar los directorios
`deploy/prover/` y `deploy/verifier/` correspondientes al estado de
**500 alumnos**. Ejemplo si existe conectividad SSH hacia ambas VM Azure:

```bash
rsync -av deploy/prover/ usuario@IP_PUBLICA_VM_PROVER:~/TT-IntegriDB/deploy/prover/
rsync -av deploy/verifier/ usuario@IP_PUBLICA_VM_VERIFIER:~/TT-IntegriDB/deploy/verifier/
```

Si las VM Azure no tienen IP pública, transferir los archivos utilizando
un salto SSH autorizado (o una conexión VPN) en lugar de habilitar acceso
público a los servicios. **No copiar `keys/`, `secret_key.txt` ni la base
privada del propietario.**

Comprobar en cada VM que los archivos requeridos están presentes:

- Probador: `deploy/prover/authenticated_db.bin`, `delta.txt`, `public_key.txt`.
- Verificador: `deploy/verifier/delta.txt`, `public_key.txt`.

## 5. Arrancar probador (VM Azure probador)

```bash
cd ~/TT-IntegriDB
read -r -p 'FQDN de Azure MySQL: ' TT_DB_HOST
read -r -p 'IP PRIVADA de esta VM: ' TT_AZURE_PROVER_IP
export TT_DB_USER=tt_reader
export TT_ENABLE_DIRECT_BENCH=1
read -s -r -p 'Clave de tt_reader: ' TT_DB_PASS; echo
export TT_DB_PASS TT_DB_HOST TT_DB_USER TT_AZURE_PROVER_IP TT_ENABLE_DIRECT_BENCH

docker compose -f compose.azure.prover.yml up -d --build
docker compose -f compose.azure.prover.yml ps
docker compose -f compose.azure.prover.yml logs --tail=80 prover
curl -fsS "http://${TT_AZURE_PROVER_IP}:7070/v1/health"
```

En este Compose, `TT_DB_TLS_MODE=VERIFY_IDENTITY` está habilitado. El
probador valida la cadena de confianza mediante las CA raíz del sistema y
que el FQDN de `TT_DB_HOST` corresponda al certificado del servicio.
No desactivar `require_secure_transport` en Azure para evitar fallas de
conexión; revisar antes DNS privado, CA, hora del sistema y credenciales.

**Nota de seguridad:** las variables de entorno Docker pueden ser visibles
para usuarios con acceso a Docker. En este entorno experimental, limitar
permisos de administración y no compartir su contenido. Para un despliegue
productivo, sustituir estas credenciales por un gestor de secretos.

## 6. Arrancar verificador (VM Azure verificador)

```bash
cd ~/TT-IntegriDB
read -r -p 'IP PRIVADA VM probador: ' TT_PROVER_PRIVATE_IP
export TT_PROVER_ADDR="${TT_PROVER_PRIVATE_IP}:7070"
export TT_ENABLE_DIRECT_BENCH=1

docker compose -f compose.azure.verifier.yml up -d --build
docker compose -f compose.azure.verifier.yml ps
curl -fsS http://127.0.0.1:8080/api/status
python3 tools/experimentos/validar_equivalencia.py \
  --url http://127.0.0.1:8080 --all --timeout 600
```

El frontend se publica **solo en la IP de loopback de la VM verificador**.
Para abrirlo desde tu laptop, inicia un túnel SSH en una terminal **local**:

```bash
ssh -L 8080:127.0.0.1:8080 usuario@IP_PUBLICA_VM_VERIFIER
```

Con el túnel abierto, navega a `http://127.0.0.1:8080` en la laptop.
No abrir 8080 en la regla NSG. El navegador en la laptop añade latencia de
Internet al tiempo total medido: **no tratarla como equivalente al
navegador situado dentro de la VM de VirtualBox**. Para un experimento
controlado, registrar dónde se ejecutó el navegador y mantenerlo constante.

## 7. Protocolo experimental de 500 alumnos

1. Comprobar `C1–C14` con `validar_equivalencia.py --all`.
2. Confirmar el identificador de estado publicado y que las respuestas sean
   verificadas. Si falla, revisar los datos importados antes de generar
   cualquier estado criptográfico nuevo.
3. En el frontend ejecutar **5 pares de calentamiento y 30 pares medidos**
   para cada consulta, alternando verificable y SQL directo.
4. Conservar los parámetros del experimento de 500 alumnos en VirtualBox;
   exportar un CSV para cada consulta. Registrar región, SKU, versión de
   MySQL, vCPU, RAM y navegador.
5. Mantener activado `TT_ENABLE_DIRECT_BENCH=1` **solo durante los
   experimentos**. Esta modalidad de SQL directo no autentica resultados.
6. Comparar de manera separada tiempo total del navegador, generación en
   probador, verificación y tamaño de respuesta. No atribuir todo el tiempo
   a operaciones criptográficas: la consulta directa aprovecha el
   optimizador SQL y el recorrido verificable incluye consultas auxiliares.

Las operaciones de `Setup` no se repiten, porque este escenario utiliza
el **mismo conjunto de datos y estado autenticado** de 500 alumnos.
Si se usa otra base o se altera la semilla, deben repetirse las fases
criptográficas apropiadas en el propietario.

## 8. Al concluir la sesión

Detener los contenedores en cada VM:

```bash
docker compose -f compose.azure.prover.yml stop prover
# En la otra VM:
docker compose -f compose.azure.verifier.yml stop verifier
```

**También** detener y desasignar las VM desde Azure, y detener el servidor
Flexible Server si no se utilizará. El disco y almacenamiento pueden seguir
acumulando cargos, aun cuando el cómputo esté detenido. Verificar el estado
real de los recursos y el presupuesto en Azure Portal.

## Referencias oficiales

- [Red privada y DNS](https://learn.microsoft.com/en-us/azure/mysql/flexible-server/concepts-networking-vnet)
- [TLS y certificados](https://learn.microsoft.com/en-us/azure/mysql/flexible-server/security-tls)
- [Conexiones TLS](https://learn.microsoft.com/en-us/azure/mysql/flexible-server/security-tls-how-to-connect)
- [Creación mediante Azure Portal](https://learn.microsoft.com/en-us/azure/mysql/flexible-server/quickstart-create-server-portal)
- [Precios](https://azure.microsoft.com/en-us/pricing/details/mysql/)
