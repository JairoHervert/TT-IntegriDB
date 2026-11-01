# Despliegue en la nube

El esquema se reparte igual que en la arquitectura del reporte. El servidor
no confiable queda en la nube y lo confiable se queda en la computadora del
cliente:

| Dónde | Qué corre | Qué tiene | Archivo |
|---|---|---|---|
| Nube (no confiable) | MySQL (`tt_db`) y el probador (`tt_prover serve`) | D, D~, δ y pk | `compose.server.yml` |
| Computadora del cliente (confiable) | verificador con la interfaz (`tt_verifier ui`) | δ y pk | `compose.client.yml` |
| Computadora del propietario (confiable) | `tt_owner init`, `setup` y `publish` | la clave secreta | fuera de Docker |

La computadora del propietario y la del cliente pueden ser la misma. Lo
importante es que el verificador nunca corra en la nube: ahí se decidiría
el veredicto en la misma máquina que podría alterar los datos.

```
 computadora (confiable)                          nube (no confiable)
 ┌─────────────────────────────┐   internet   ┌──────────────────────────┐
 │ navegador ─ tt_verifier ui  │ ───────────> │ tt_prover :7070          │
 │ 127.0.0.1:8080   δ + pk     │   (R, π)     │   δ + D~ + pk            │
 │ tt_owner (clave secreta)    │ <─────────── │ tt_db (MySQL, sin puerto │
 └─────────────────────────────┘              │        publicado)        │
                                              └──────────────────────────┘
```

## 1. La máquina en la nube

Cualquier proveedor sirve (Azure for Students, AWS, Google Cloud,
DigitalOcean...). Lo que importa:

- **Sistema:** Ubuntu 24.04 LTS, la misma base que las imágenes.
- **Tamaño:** al menos 2 vCPU y 4 GB de RAM. Con 1 GB la compilación dentro
  de Docker puede quedarse sin memoria.
- **Para medir tiempos**, evita las máquinas "burstable" o de núcleo
  compartido (Azure serie B, AWS t2/t3/t4g, Google e2-micro/small/medium).
  Cuando se les acaban los créditos de CPU, bajan la velocidad, y los
  tiempos de ProofGen cambian de una corrida a otra. Una de núcleos
  dedicados es más estable, por ejemplo Azure D2s, AWS m6i.large o Google
  n2-standard-2. Anota en el reporte el tipo de máquina y la región.
- **Firewall del proveedor** (Network Security Group, Security Group o
  reglas de VPC). Solo se abren dos puertos de entrada:
  - 22 (SSH) y 7070 (probador), y solo desde las IP públicas de ustedes.
  - **No se abren** 3306 ni 8080. Además, MySQL no publica ningún puerto
    en `compose.server.yml`.
  - El firewall se configura en el proveedor, no con `ufw` dentro de la
    máquina, porque Docker abre sus puertos por fuera de `ufw`.

Instalar Docker en la máquina de la nube (Ubuntu 24.04):

```bash
sudo apt update
sudo apt install -y docker.io docker-compose-v2 git
sudo usermod -aG docker $USER      # cerrar sesión y volver a entrar
```

## 2. El propietario prepara y publica (en su computadora)

Igual que en la VM. El estado se calcula sobre la base local, que se cargó
con el mismo `academic_records_db/init/02_seed_data.sql` que usará la nube;
por eso δ coincide con lo que va a tener el servidor. La base local no debe
tener cambios después de cargarse; si los tiene, vuelve a crearla con
`docker compose down -v && docker compose up -d db` antes de Setup.

```bash
export TT_DB_PASS=app_pass_2026
./build/owner/tt_owner setup                     # sobre la base local
./build/owner/tt_owner publish --keys keys --state setup --out deploy
```

Después se sube a la nube **solo** `deploy/prover`: δ, D~ y pk. Pesan
unos 12 MB y no incluyen la clave secreta.

```bash
ssh usuario@IP 'git clone <url-del-repositorio> TT-IntegriDB && mkdir -p TT-IntegriDB/deploy'
scp -r deploy/prover usuario@IP:TT-IntegriDB/deploy/
```

`scp` y `ssh` también existen en PowerShell de Windows 10 y 11.

## 3. Levantar la nube

En la máquina de la nube:

```bash
cd TT-IntegriDB
export TT_DB_PASS=app_pass_2026
docker compose -f compose.server.yml up -d --build
docker compose -f compose.server.yml ps          # esperar "healthy" en tt_db y tt_prover
docker compose -f compose.server.yml logs -f prover
```

La primera vez construye la imagen del probador (compila y corre las 40
pruebas) y carga la base. Tarda unos minutos.

## 4. Usarla desde la computadora del cliente

Primero se comprueba que el probador responde con el mismo δ:

```bash
./build/verifier/tt_verifier ping --server IP:7070 --delta deploy/verifier/delta.txt
```

Después hay dos formas de usarla.

Interfaz con Docker:

```bash
export TT_PROVER_ADDR=IP:7070                  # PowerShell: $env:TT_PROVER_ADDR = "IP:7070"
docker compose -f compose.client.yml up -d --build
```

Interfaz o consultas sin Docker:

```bash
./build/verifier/tt_verifier ui --server IP:7070 --keys deploy/verifier --delta deploy/verifier/delta.txt
./build/verifier/tt_verifier query C14 1 --server IP:7070 --keys deploy/verifier --delta deploy/verifier/delta.txt
```

En los dos casos la interfaz queda en `http://127.0.0.1:8080`.

## 5. Medir tiempos

Cada consulta deja tres números en dos lugares:

| Dónde | Mensaje | Qué mide |
|---|---|---|
| Cliente (`tt_verifier` o bitácora de la interfaz) | `respuesta del probador ...: 41.9 KB recibidos en X s` | ida y vuelta: red más ProofGen |
| Bitácora del probador (`docker compose -f compose.server.yml logs prover`) | `"C14 1" -> 200 (41.9 KB) en Y s` | ProofGen en el servidor, con la lectura de MySQL |
| Cliente | `RESPUESTA VERIFICADA ... en Z s` | Verify en el cliente |

El costo de la red es aproximadamente X − Y.

Para que las mediciones sean comparables:

- Repite cada consulta varias veces y usa la mediana. La primera corrida
  después de levantar el probador suele ser más lenta.
- Anota el tipo de máquina, la región y desde dónde se conectó el cliente.
- No compares tiempos de máquinas de núcleo compartido (sección 1).

## 6. Demostración de una base alterada en la nube

Por SSH en la máquina de la nube:

```bash
docker exec -e MYSQL_PWD="$TT_DB_PASS" tt_db mysql -uapp_user academic_db -e "UPDATE academic_record SET grade = 8 WHERE record_id = 14"
```

El cliente rechaza C2, C4 y C14 señalando la fila 14. Para regresarla, se
repite con `grade = 6`.

## 7. Si cambia el estado

Si el propietario vuelve a ejecutar Setup:

1. En su computadora: publica de nuevo.
2. Sube otra vez `deploy/prover`:
   ```bash
   scp -r deploy/prover usuario@IP:TT-IntegriDB/deploy/
   ```
3. En la nube, reinicia el probador:
   ```bash
   docker compose -f compose.server.yml restart prover
   ```
4. En el cliente, reinicia la interfaz para que lea el `deploy/verifier`
   nuevo.

Si el cliente se queda con un δ viejo, `ping` lo avisa y todas las
respuestas se rechazan (RNF08).

## Seguridad en este despliegue

- **Integridad:** no depende de la nube ni de internet. Cada respuesta se
  verifica en la computadora del cliente, y una alteración en la base, en
  el probador o en el camino se rechaza.
- **Confidencialidad:** no es un objetivo del TT. Los datos viajan sin
  cifrar y cualquiera que llegue al puerto 7070 puede consultar. Por eso el
  firewall solo admite las IP de ustedes. Los datos son sintéticos.
- **La clave secreta nunca sale de la computadora del propietario:** a la
  nube solo va `deploy/prover`.

## Cómo se probó

Se simuló el despliegue en un mismo equipo, con dos proyectos de Compose
separados que solo se comunican por la IP del equipo, como si fuera
internet:

- `compose.server.yml` dejó `tt_db` y `tt_prover` sanos.
- `compose.client.yml`, con `TT_PROVER_ADDR=<IP>:7070`, dejó `tt_verifier`
  sano con el mismo δ.
- El expediente completo y el catálogo se verificaron desde el navegador.
- C8 (7.5 MB) se verificó con `tt_verifier query --server <IP>:7070` fuera
  de Docker.
- El puerto 3306 rechazó conexiones desde fuera de la nube.
