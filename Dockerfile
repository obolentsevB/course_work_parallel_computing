# Build stage: compile the server and the CLI.
FROM ubuntu:24.04 AS build
RUN apt-get update \
 && apt-get install -y --no-install-recommends g++ cmake make ca-certificates \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt .
COPY src src
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j"$(nproc)"

# Runtime stage: only binaries and static files.
FROM ubuntu:24.04
COPY --from=build /src/build/server /src/build/search /usr/local/bin/
EXPOSE 8080
CMD ["server"]