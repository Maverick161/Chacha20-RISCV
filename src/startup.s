# RISC-V startup assembly for C programs
# Sets up stack and calls C main function

.section .text
.globl _start

_start:
    # Initialize stack pointer to top of stack
    la sp, _stack_top
    
    # Call C main function
    call main
    
    # If main returns, loop infinitely
_halt:
    j _halt