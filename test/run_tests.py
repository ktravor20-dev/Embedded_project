#!/usr/bin/env python3
"""Compile actual core C modules with host-only substitutes for hardware services."""
from pathlib import Path
import subprocess
import re
root = Path(__file__).resolve().parents[1]
work = root / 'work' / 'host-tests'
work.mkdir(parents=True, exist_ok=True)
header = (root / 'include/smart_cocis.h').read_text()
header = re.sub(r'#include "(?:esp[^\"]*|freertos/[^\"]*)"\n', '', header)
stubs = '''#include <stdlib.h>
#include <stdint.h>
typedef int esp_err_t;
typedef int SemaphoreHandle_t;
typedef int QueueHandle_t;
typedef void (*TaskFunction_t)(void *);
typedef unsigned UBaseType_t;
#define portMAX_DELAY 0
#define xSemaphoreTake(a,b) ((void)0)
#define xSemaphoreGive(a) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_OK 0
#define pdPASS 1
#define pdMS_TO_TICKS(x) (x)
#define xQueueReceive(a,b,c) 0
#define vTaskDelay(x) ((void)0)
'''
(work / 'smart_cocis.h').write_text(stubs + header)
(work / 'nvs.h').write_text('''#pragma once
#include <stdint.h>
typedef int nvs_handle_t;
#define NVS_READONLY 0
#define NVS_READWRITE 1
int nvs_open(const char *, int, nvs_handle_t *);
int nvs_get_u32(nvs_handle_t, const char *, uint32_t *);
int nvs_set_u32(nvs_handle_t, const char *, uint32_t);
int nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);
''')
subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-I'+str(work), str(root/'test/test_core.c'), '-o', str(work/'test_core')], check=True)
subprocess.run([str(work/'test_core')], check=True)
# Verify explanatory comments immediately precede all production function definitions.
count = 0
for path in [root/'src/main.c', *sorted((root/'files_to_work_in').glob('*/*.c'))]:
    text = path.read_text()
    for match in re.finditer(r'^(?:static )?(?:const )?[\w]+[ *]+\w+\([^;]*?\)\n\{', text, re.M):
        assert text[:match.start()].rstrip().endswith('*/'), f'Missing function comment: {path}: {match.group(0)}'
        count += 1
print(f'PASS: explanatory comments precede all {count} production function definitions.')
