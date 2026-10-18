#!/usr/bin/env python3
"""test-vectors.json -> common/test/test_vectors.h"""
import json
import pathlib

root = pathlib.Path(__file__).resolve().parents[1]
data = json.loads((root / "test-vectors.json").read_text())
lines = [
    "/* tools/gen_vectors.py tarafından üretildi — elle düzenleme. */",
    "#ifndef TEST_VECTORS_H",
    "#define TEST_VECTORS_H",
    "",
    "#include <stdint.h>",
    "",
    "typedef struct {",
    "    uint16_t value;",
    "    uint8_t telemetry;",
    "    uint8_t inverted;",
    "    uint16_t frame;",
    "} dshot_vector_t;",
    "",
    "static const dshot_vector_t DSHOT_VECTORS[] = {",
]
for v in data["vectors"]:
    lines.append(f"    {{{v['value']}u, {int(v['telemetry'])}u, {int(v['inverted'])}u, {v['frame']}u}},")
lines += [
    "};",
    "#define DSHOT_VECTOR_COUNT (sizeof(DSHOT_VECTORS) / sizeof(DSHOT_VECTORS[0]))",
    "",
    "#endif",
    "",
]
(root / "common" / "test" / "test_vectors.h").write_text("\n".join(lines))
