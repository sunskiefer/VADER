#!/bin/sh
# Assemble the patch sources to raw binaries at their load addresses. Run in an arm32v7 gcc container with this folder mounted:
#   docker run --rm --platform linux/arm/v7 -v "$PWD/patch":/p -w /p arm32v7/gcc:12 sh asm.sh
set -e
mkdir -p out
asm() { as -march=armv7-a "$1.S" -o out/$1.o
        ld -Ttext="$2" --no-dynamic-linker -e _start out/$1.o -o out/$1.elf 2>/dev/null
        objcopy -O binary out/$1.elf out/$1.bin; }
asm helper 0x2494954; asm cave2 0x4a7b330; asm colours_jump 0x2494bb4; asm colours 0x6872900; asm matcher 0x6872980
ls -l out/*.bin
