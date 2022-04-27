.syntax unified
.cpu cortex-m0
.fpu softvfp
.thumb

.global soft_reset
.type soft_reset, %function
soft_reset:
  ldr  r0, =0xE000ED0C
  ldr  r1, =0x05FA0004
  str  r1, [r0]
soft_reset_loop:
  b    soft_reset_loop
