# ══════════════════════════════════════════════════════════════════════
#  Dockerfile — Ambiente de build e execução
#  Base: debian:stable-slim  |  Compilador: clang  |  Build: cmake
# ══════════════════════════════════════════════════════════════════════

FROM debian:stable-slim AS base

# ── Sistema e ferramentas ───────────────────────────────────────────────
RUN apt-get update && apt-get install -y --no-install-recommends \
        # Compilador preferido (mensagens de erro mais amigáveis)
        clang          \
        # Sistema de build moderno
        cmake          \
        make           \
        # Valgrind: memcheck (vazamentos) + Helgrind (race conditions)
        valgrind       \
        # Utilitários extras úteis no desenvolvimento
        ca-certificates \
        procps          \
    && rm -rf /var/lib/apt/lists/*

# Garante que 'cc' aponte para clang (usado pelo cmake como fallback)
RUN update-alternatives --install /usr/bin/cc  cc  /usr/bin/clang   100 && \
    update-alternatives --install /usr/bin/c99 c99 /usr/bin/clang   100

# ── Diretórios da aplicação ─────────────────────────────────────────────
WORKDIR /app

# Pasta de logs — será mapeada como volume no docker-compose
RUN mkdir -p /logs

# ── Cópia e compilação do código-fonte ─────────────────────────────────
COPY . .

# Compila com CMake em modo Debug para manter símbolos do Valgrind
RUN cmake -S . -B build \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_C_COMPILER=clang \
    && cmake --build build --parallel "$(nproc)"

# ══════════════════════════════════════════════════════════════════════
#  Notas de uso do Valgrind:
#
#  Helgrind (race conditions):
#    docker exec -it servidor-estoque \
#      valgrind --tool=helgrind --log-file=/logs/helgrind.log /app/build/servidor
#
#  Memcheck (vazamentos):
#    docker exec -it servidor-estoque \
#      valgrind --leak-check=full --track-origins=yes \
#               --log-file=/logs/memcheck.log /app/build/servidor
# ══════════════════════════════════════════════════════════════════════
