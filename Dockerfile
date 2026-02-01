FROM debian:bookworm-slim

# Instalar dependencias de construcción
RUN apt-get update && apt-get install -y \
    build-essential \
    autoconf \
    automake \
    libtool \
    pkg-config \
    libusb-1.0-0-dev \
    libplist-dev \
    libimobiledevice-dev \
    libimobiledevice-glue-dev \
    libudev-dev \
    systemd \
    git \
    && rm -rf /var/lib/apt/lists/*

# Copiar el código fuente
WORKDIR /usr/src

# Copiar archivos necesarios para la construcción
COPY . .
COPY --from=ghcr.io/libimobiledevice/idevicerestore:latest /usr/local/lib/libimobiledevice-glue-1.0.so.* /usr/local/lib/ || true

# Compilar e instalar usbmuxd
RUN ./autogen.sh \
    --without-preflight \
    --without-udev \
    --without-systemd \
    && make \
    && make install \
    && ldconfig

# Puerto por defecto
EXPOSE 27015

# Comando por defecto
CMD ["/usr/local/sbin/usbmuxd"]
