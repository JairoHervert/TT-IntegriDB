# TT 2026-B143 - imagenes del probador y del verificador
#
# Un solo Dockerfile con tres etapas:
#   build     compila ate-pairing y todo el proyecto, y corre ctest; si una
#             prueba falla, la imagen no se construye.
#   prover    bloque Servidor: solo tt_prover y las bibliotecas que usa.
#   verifier  bloque Cliente: solo tt_verifier, la pagina de ui/web y las
#             bibliotecas que usa (sin cliente de MySQL).
#
# Ninguna imagen contiene claves, delta ni D~: llegan como volumenes de solo
# lectura desde deploy/ (ver docker-compose.yml y docs/contenedores.md). La
# clave secreta nunca entra al contexto de construccion (.dockerignore).
#
# Construir a mano (normalmente lo hace docker compose):
#   docker build --target prover   -t tt-integridb/prover .
#   docker build --target verifier -t tt-integridb/verifier .

ARG UBUNTU_VERSION=24.04

# --- Etapa 1: compilacion -------------------------------------------------
FROM ubuntu:${UBUNTU_VERSION} AS build

# ate-pairing fijado en un commit, para que la VM y las imagenes usen
# exactamente la misma biblioteca de emparejamientos.
ARG ATE_PAIRING_COMMIT=530223d7502e95f6141be19addf1e24d27a14d50

RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      build-essential cmake git ca-certificates \
      libgmp-dev libntl-dev libssl-dev libmysqlclient-dev \
 && rm -rf /var/lib/apt/lists/*

RUN git clone https://github.com/herumi/ate-pairing.git /opt/ate-pairing \
 && git -C /opt/ate-pairing checkout --quiet "${ATE_PAIRING_COMMIT}" \
 && make -C /opt/ate-pairing/src -j"$(nproc)" > /dev/null

# Solo se copian las carpetas del codigo, nunca toda la raiz: asi una
# compilacion local (build/) o las claves (keys/) no pueden terminar dentro
# de la imagen aunque falte el .dockerignore.
WORKDIR /src
COPY CMakeLists.txt ./
COPY third_party ./third_party
COPY core ./core
COPY cli ./cli
COPY mysql ./mysql
COPY net ./net
COPY ui ./ui
COPY owner ./owner
COPY prover ./prover
COPY verifier ./verifier
COPY tests ./tests
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DATE_PAIRING_DIR=/opt/ate-pairing \
 && cmake --build build -j"$(nproc)" \
 && ctest --test-dir build --output-on-failure

# --- Etapa 2: probador (bloque Servidor) -----------------------------------
FROM ubuntu:${UBUNTU_VERSION} AS prover

RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      libntl44 libssl3t64 libmysqlclient21 ca-certificates \
 && rm -rf /var/lib/apt/lists/* \
 && useradd --system --uid 10001 --no-create-home --shell /usr/sbin/nologin tt

COPY --from=build /src/build/prover/tt_prover /usr/local/bin/tt_prover

# El estado (delta, D~ y pk) se monta aqui en solo lectura.
VOLUME ["/srv/tt/prover"]
USER tt
WORKDIR /srv/tt
EXPOSE 7070

# Sano = /v1/health responde. Sin curl ni wget: bash abre la conexion.
HEALTHCHECK --interval=5s --timeout=3s --start-period=20s --retries=5 \
  CMD ["bash", "-c", "exec 3<>/dev/tcp/127.0.0.1/7070 && printf 'GET /v1/health HTTP/1.0\\r\\nHost: localhost\\r\\n\\r\\n' >&3 && grep -q 'tt-integridb prover' <&3"]

ENTRYPOINT ["tt_prover"]
CMD ["serve", "--state", "/srv/tt/prover", "--keys", "/srv/tt/prover", "--host", "0.0.0.0", "--port", "7070"]

# --- Etapa 3: verificador con la interfaz (bloque Cliente) ------------------
FROM ubuntu:${UBUNTU_VERSION} AS verifier

RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      libntl44 libssl3t64 \
 && rm -rf /var/lib/apt/lists/* \
 && useradd --system --uid 10001 --no-create-home --shell /usr/sbin/nologin tt

COPY --from=build /src/build/verifier/tt_verifier /usr/local/bin/tt_verifier
COPY ui/web /srv/tt/web
# La pagina la lee el usuario tt: se dan permisos de lectura sin importar
# con que permisos llegaron los archivos desde la maquina que construye.
RUN chmod -R a+rX /srv/tt/web

# delta y pk (lo que publico el propietario para el cliente), solo lectura.
VOLUME ["/srv/tt/verifier"]
USER tt
WORKDIR /srv/tt
EXPOSE 8080

# Sano = la interfaz entrega su pagina.
HEALTHCHECK --interval=5s --timeout=3s --start-period=10s --retries=5 \
  CMD ["bash", "-c", "exec 3<>/dev/tcp/127.0.0.1/8080 && printf 'GET / HTTP/1.0\\r\\nHost: localhost\\r\\n\\r\\n' >&3 && head -n 1 <&3 | grep -q ' 200 '"]

ENTRYPOINT ["tt_verifier"]
CMD ["ui", "--keys", "/srv/tt/verifier", "--delta", "/srv/tt/verifier/delta.txt", "--host", "0.0.0.0", "--port", "8080", "--web", "/srv/tt/web"]
