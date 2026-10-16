@ At the old cave 0x4a7b330 (80 zero bytes in the stock binary): jump to the name matcher at 0x6872980.
@ The helper at 0x2494954 jumps here with r0 = juce::String*, r2 = its text, lr = return address (unchanged).
        .arm
        .text
        .globl _start
_start:
        ldr   ip, [pc, #0]
        add   pc, pc, ip
        .word 0x6872980 - (0x4a7b330 + 4 + 8)
