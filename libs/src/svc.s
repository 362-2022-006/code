.syntax unified
.cpu cortex-m0
.fpu softvfp
.thumb

.text
.balign 2

.global SVC_Handler
.type SVC_Handler, %function
SVC_Handler:
    push {lr}
    ldr  r0, [sp, #0x1C]

    movs r1, r0
    subs r1, #2
    str  r1, [sp, #0x1C]

    bl   SVC_function
    movs r4, r0
    pop  {pc}
