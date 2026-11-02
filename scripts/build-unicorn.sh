#!/bin/sh
set -eu

# Builds Unicorn 2.1.4 (MIPS only) into DIR for the MIPS backend's host
# tests (tests/core/mips_unicorn.c): sh scripts/build-unicorn.sh DIR, then
# UNICORN_DIR=DIR for make check-core.
#
# Stock Unicorn runs MIPS32 and MIPS32r2 code (ext, ins, rotr, rotrv), but
# the backend also emits four Allegrex instructions that MIPS32 lacks or
# encodes elsewhere: clz and clo as SPECIAL functions 0x16 and 0x17, max
# and min as 0x2C and 0x2D (PPSSPP's MIPSTables.cpp lists them). Unicorn
# treats those as reserved instructions and loses the pc, so this teaches
# its decoder them.

dir=${1:?usage: build-unicorn.sh DIR}
version=2.1.4
mkdir -p "$dir"
cd "$dir"
if [ ! -d "unicorn-$version" ]; then
  curl -sL -o unicorn.tar.gz \
    "https://github.com/unicorn-engine/unicorn/archive/refs/tags/$version.tar.gz"
  tar xzf unicorn.tar.gz
fi
cd "unicorn-$version"
python3 - qemu/target/mips/translate.c <<'EOF'
import sys

path = sys.argv[1]
text = open(path).read()
anchor = '''    case OPC_SPIM:
#ifdef MIPS_STRICT_STANDARD
        MIPS_INVAL("SPIM");'''
allegrex = '''    case 0x16: /* Allegrex CLZ */
    case 0x17: /* Allegrex CLO */
        gen_cl(ctx, op1 == 0x16 ? OPC_CLZ : OPC_CLO, rd, rs);
        break;
    case 0x2C: /* Allegrex MAX */
    case 0x2D: /* Allegrex MIN */
        if (rd != 0) {
            TCGContext *tcg_ctx = ctx->uc->tcg_ctx;
            TCGv t0 = tcg_temp_new(tcg_ctx);
            TCGv t1 = tcg_temp_new(tcg_ctx);
            gen_load_gpr(tcg_ctx, t0, rs);
            gen_load_gpr(tcg_ctx, t1, rt);
            if (op1 == 0x2C) {
                tcg_gen_smax_tl(tcg_ctx, tcg_ctx->cpu_gpr[rd], t0, t1);
            } else {
                tcg_gen_smin_tl(tcg_ctx, tcg_ctx->cpu_gpr[rd], t0, t1);
            }
            tcg_temp_free(tcg_ctx, t0);
            tcg_temp_free(tcg_ctx, t1);
        }
        break;
'''
if 'Allegrex CLZ' not in text:
    if text.count(anchor) != 1:
        sys.exit('translate.c: the SPECIAL decoder is not where expected')
    open(path, 'w').write(text.replace(anchor, allegrex + anchor))
EOF
mkdir -p build
cd build
cmake .. -DUNICORN_ARCH=mips -DCMAKE_BUILD_TYPE=Release \
  -DUNICORN_BUILD_TESTS=OFF > cmake.log
make -j4 unicorn > make.log
echo "Unicorn with the Allegrex instructions: $dir/unicorn-$version"
