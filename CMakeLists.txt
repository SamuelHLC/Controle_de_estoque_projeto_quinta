cmake_minimum_required(VERSION 3.20)
project(EstoqueDistribuido C)

# ── Padrão C e flags de otimização ─────────────────────────────────────
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Aproveita paralelismo do processador (vetorização automática + native arch)
# -march=native instrui o compilador a usar todas as extensões do CPU atual
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -march=native -Wall -Wextra")

# ── Flags específicas de build ──────────────────────────────────────────
# Debug: preserva símbolos para Valgrind / Helgrind
# Nota: -fsanitize=thread removido pois requer libclang_rt.tsan que não vem
# no debian:stable-slim. Use o Helgrind do Valgrind para race conditions.
set(CMAKE_C_FLAGS_DEBUG   "-g3 -O0")
# Release: otimização máxima
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")

# ── Dependências ────────────────────────────────────────────────────────
find_package(Threads REQUIRED)   # resolve -pthread portavelmente

# librt: funções POSIX de tempo real (clock_gettime, shm_open, etc.)
# Necessária em glibc < 2.17; no Debian Bookworm já está embutida na libc,
# mas deixamos o link explícito para garantir compatibilidade.
find_library(RT_LIB rt)

# ── Executável: servidor ────────────────────────────────────────────────
add_executable(servidor servidor.c)
target_link_libraries(servidor
    PRIVATE
        Threads::Threads          # -pthread (compile + link)
        $<$<BOOL:${RT_LIB}>:rt>  # -lrt somente se encontrado
)
target_compile_options(servidor PRIVATE -pthread)

# ── Executável: cliente ─────────────────────────────────────────────────
add_executable(cliente cliente.c)
target_link_libraries(cliente
    PRIVATE
        Threads::Threads
        $<$<BOOL:${RT_LIB}>:rt>
)
target_compile_options(cliente PRIVATE -pthread)

# ── Executável: admin ───────────────────────────────────────────────────
add_executable(admin admin.c)
target_link_libraries(admin
    PRIVATE
        Threads::Threads
        $<$<BOOL:${RT_LIB}>:rt>
)
target_compile_options(admin PRIVATE -pthread)

# ── Mensagem de resumo ──────────────────────────────────────────────────
message(STATUS "Compilador  : ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
message(STATUS "Flags base  : ${CMAKE_C_FLAGS}")
message(STATUS "Build type  : ${CMAKE_BUILD_TYPE}")