#!/bin/sh
set -eu
trap 'docker compose down --volumes --remove-orphans' EXIT
docker compose up --build --exit-code-from qa-runner --abort-on-container-exit
