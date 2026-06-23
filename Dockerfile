FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    gcc \
    make \
    nodejs \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY . .

# Build ckociemba (uses its own Makefile with -O3, no changes needed)
RUN make -C third_party/ckociemba

# Build cubyte without sanitizers or debug symbols for production
RUN make cubyte CFLAGS="-std=c17 -Wall -Wextra -O2 -D_POSIX_C_SOURCE=200809L"

EXPOSE 3000

CMD ["node", "visualiser/server.js"]
