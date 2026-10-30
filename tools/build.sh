#!/bin/sh
# Build with an activated ESP-IDF 6.1 environment, or the installed Mac toolchain.
set -eu
if [ -z "${IDF_PATH:-}" ]; then
    export IDF_PATH='/Users/ktravor/esp-idf installation/.espressif/v6.1/esp-idf'
    export IDF_PYTHON_ENV_PATH=/Users/ktravor/.espressif/python_env/idf6.1_py3.13_env
    export PATH="$IDF_PYTHON_ENV_PATH/bin:/Users/ktravor/.espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin:/Users/ktravor/.espressif/tools/ninja/1.12.1:/opt/homebrew/bin:$PATH"
fi
export ESP_IDF_VERSION=6.1
# All dependencies are bundled IDF components; no registry lookup is needed.
export IDF_COMPONENT_MANAGER=0
cd "$(dirname "$0")/.."
python "$IDF_PATH/tools/idf.py" -B build-s3 -D SDKCONFIG=sdkconfig.s3 -D IDF_TARGET=esp32s3 "${@:-build}"
