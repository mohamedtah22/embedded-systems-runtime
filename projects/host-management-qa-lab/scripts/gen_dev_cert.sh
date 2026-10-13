#!/bin/sh
set -eu
out="${1:-certs}"
name="${2:-localhost}"
mkdir -p "$out"
# Ephemeral TEST cert only. Never commit private keys or use in production.
openssl req -newkey rsa:2048 -nodes -x509 -sha256 -days 1 \
  -subj "/CN=$name" -addext "subjectAltName=DNS:$name,DNS:localhost,IP:127.0.0.1" \
  -keyout "$out/key.pem" -out "$out/cert.pem" >/dev/null 2>&1
chmod 600 "$out/key.pem"
