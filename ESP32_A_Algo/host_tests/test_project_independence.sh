#!/bin/sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

for REQUIRED_FILE in \
    main/camera_service.cpp \
    main/camera_service.h \
    main/camera_pins.h \
    main/idf_component.yml \
    dependencies.lock
do
    if test ! -f "$PROJECT_DIR/$REQUIRED_FILE"; then
        echo "missing project-owned camera file: $REQUIRED_FILE" >&2
        exit 1
    fi
done

if find "$PROJECT_DIR/main" -type l -print | grep -q .; then
    echo "main/ must not use symlinks to external source files" >&2
    exit 1
fi

for BUILD_INPUT in \
    "$PROJECT_DIR/CMakeLists.txt" \
    "$PROJECT_DIR/main/CMakeLists.txt" \
    "$PROJECT_DIR/main/idf_component.yml"
do
    if grep -En 'ESP_Camera|ESP32_CAMERA|\.\./|EXTRA_COMPONENT_DIRS' \
        "$BUILD_INPUT"; then
        echo "external project reference in build input: $BUILD_INPUT" >&2
        exit 1
    fi
done

if ! grep -Eq 'espressif/esp32-camera:[[:space:]]*$' \
    "$PROJECT_DIR/main/idf_component.yml" ||
   ! grep -Eq 'version:[[:space:]]*"2\.1\.7"[[:space:]]*$' \
    "$PROJECT_DIR/main/idf_component.yml"; then
    echo "esp32-camera 2.1.7 must remain pinned in the local manifest" >&2
    exit 1
fi

if ! grep -Eq 'set\(IDF_TARGET[[:space:]]+"esp32s3"' \
    "$PROJECT_DIR/CMakeLists.txt" ||
   ! grep -Eq '^CONFIG_PARTITION_TABLE_SINGLE_APP=y$' \
    "$PROJECT_DIR/sdkconfig.defaults" ||
   ! grep -Eq '^CONFIG_PARTITION_TABLE_OFFSET=0x8000$' \
    "$PROJECT_DIR/sdkconfig.defaults"; then
    echo "ESP32-S3 target and flash partition defaults must remain project-owned" >&2
    exit 1
fi

echo "project independence contract ok"
