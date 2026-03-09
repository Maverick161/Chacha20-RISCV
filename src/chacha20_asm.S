.section .text
.globl chacha20_quarter_round
.globl chacha20_block

#Esta funcion fue implementada para entender de forma sencilla como funciona el quarter_round usando punteros
/***chacha20_quarter_round:
    #Se reciben los argumentos desde la funcion en C
    # a0 = puntero a  
    # a1 = puntero b  
    # a2 = puntero c  
    # a3 = puntero d  

    #Se cargan los 4 valores desde los punteros
    lw t0, 0(a0)
    lw t1, 0(a1)
    lw t2, 0(a2)
    lw t3, 0(a3) 

    #Se hacen las operaciones basicas del quarter_round 
    #Linea de operaciones 1
    add t0, t0, t1 # a+=b
    xor t3, t3, t0 # d ^= a
    slli t5, t3, 16 # d = rotl(d,16)
    srli t3, t3, 16 # d = rotl(d,16)
    or t3, t5, t3 # d = rotl(d,16)

    #Linea de operaciones 2 
    add t2, t2, t3 # c+=d
    xor t1, t1, t2 # b ^= c
    slli t5, t1, 12 # b = rotl(b, 12)
    srli t1, t1, 20 # b = rotl(b, 12)
    or t1, t5, t1 # b = rotl(b, 12)

    #Linea de operaciones 3
    add t0, t0, t1 # a+=b
    xor t3, t3, t0 # d ^= a
    slli t5, t3, 8 # d = rotl(d,8)
    srli t3, t3, 24 # d = rotl(d,8)
    or t3, t5, t3 # d = rotl(d,8)

    #Linea de operaciones 4
    add t2, t2, t3 # c+=d
    xor t1, t1, t2 # b ^= c
    slli t5, t1, 7 # b = rotl(b, 7)
    srli t1, t1, 25 # b = rotl(b, 7)
    or t1, t5, t1 # b = rotl(b, 7)

    #Explicacion general de lo que hace rotl(x, x)
    #Se recibe un valor de 32 bits 
    #slli lo que hace es hacer que la parte baja del numero suba y lo guarde en un registro vacio
    #srli lo que hace es hacer que la parte alta del numero baje y lo guarde en el mismo registro
    #el or compara las nuevas partes para lograr hacer el rotl(original, x)

    #Guardar los resultados de las operaciones realizadas
    sw t0, 0(a0)
    sw t1, 0(a1)
    sw t2, 0(a2)
    sw t3, 0(a3)

    ret ***/

#Funcion oficial del quarter_round usando indicies para que sea mas sencillo de implementar en el chacha_block
    chacha20_quarter_round:
    # a0 = puntero a state[]
    # a1 = índice a 
    # a2 = índice b  
    # a3 = índice c  
    # a4 = índice d 

    #Primero se convierten los indices a offset
    slli a1, a1, 2
    slli a2, a2, 2
    slli a3, a3, 2
    slli a4, a4, 2

    #Se cargan los valores desde memoria a registros temporales
    add t0, a0, a1
    lw t0, 0(t0) #state[a]

    add t1, a0, a2
    lw t1, 0(t1) #state[b]

    add t2, a0, a3
    lw t2, 0(t2) #state[c]

    add t3, a0, a4
    lw t3, 0(t3) #state[d]

    #Se realizan las operaciones elementales del algoritmo
    #Linea de operacion 1
    add t0, t0, t1 # a+=b
    xor t3, t3, t0 # d ^= a
    slli t5, t3, 16 # d = rotl(d,16)
    srli t3, t3, 16 # d = rotl(d,16)
    or t3, t5, t3 # d = rotl(d,16)

    #Linea de operacion 2
    add t2, t2, t3 # c+=d
    xor t1, t1, t2 # b ^= c
    slli t5, t1, 12 # b = rotl(b, 12)
    srli t1, t1, 20 # b = rotl(b, 12)
    or t1, t5, t1 # b = rotl(b, 12)

    #Linea de operacion 3
    add t0, t0, t1 # a+=b
    xor t3, t3, t0 # d ^= a
    slli t5, t3, 8 # d = rotl(d,8)
    srli t3, t3, 24 # d = rotl(d,8)
    or t3, t5, t3 # d = rotl(d,8)

    #Linea de operacion 4
    add t2, t2, t3 # c+=d
    xor t1, t1, t2 # b ^= c
    slli t5, t1, 7 # b = rotl(b, 7)
    srli t1, t1, 25 # b = rotl(b, 7)
    or t1, t5, t1 # b = rotl(b, 7)

    #Guardar los resultados
    add a5, a0, a1
    sw t0, 0(a5)

    add a6, a0, a2
    sw t1, 0(a6)

    add a7, a0, a3
    sw t2, 0(a7)

    add t4, a0, a4
    sw t3, 0(t4)

    ret

#Funcion del Block oficial
    chacha20_block:
    #Se reserva espacio en el stack y se guardan registros (prologo)
    addi sp, sp, -84
    sw ra, 0(sp) #return address
    sw s0, 4(sp) #contador del loop
    sw s1, 8(sp) #puntero state original
    sw s2, 12(sp) #puntero output
    sw s3, 16(sp)

    mv s1, a0 
    mv s2, a1
    li s0, 10

    addi t0, sp, 20 #t0 = &working_state[0]
    mv s3, t0
    
    #Se realiza la copia del stack
    lw t1, 0(s1) 
    sw t1, 0(t0) 

    lw t1, 4(s1) 
    sw t1, 4(t0) 

    lw t1, 8(s1) 
    sw t1, 8(t0) 

    lw t1, 12(s1) 
    sw t1, 12(t0) 

    lw t1, 16(s1) 
    sw t1, 16(t0) 

    lw t1, 20(s1) 
    sw t1, 20(t0) 

    lw t1, 24(s1) 
    sw t1, 24(t0) 

    lw t1, 28(s1) 
    sw t1, 28(t0) 

    lw t1, 32(s1) 
    sw t1, 32(t0) 

    lw t1, 36(s1) 
    sw t1, 36(t0) 

    lw t1, 40(s1) 
    sw t1, 40(t0) 

    lw t1, 44(s1) 
    sw t1, 44(t0) 

    lw t1, 48(s1) 
    sw t1, 48(t0)

    lw t1, 52(s1) 
    sw t1, 52(t0)

    lw t1, 56(s1) 
    sw t1, 56(t0) 

    lw t1, 60(s1) 
    sw t1, 60(t0)  

    #Se inicia el loop de las columnas y diagonales
    round_loop:
        
        mv a0, s3
        li a1, 0
        li a2, 4
        li a3, 8
        li a4, 12
        call chacha20_quarter_round

        mv a0, s3
        li a1, 1
        li a2, 5
        li a3, 9
        li a4, 13
        call chacha20_quarter_round

        mv a0, s3
        li a1, 2
        li a2, 6
        li a3, 10
        li a4, 14
        call chacha20_quarter_round

        mv a0, s3
        li a1, 3
        li a2, 7
        li a3, 11
        li a4, 15
        call chacha20_quarter_round

        mv a0, s3
        li a1, 0
        li a2, 5
        li a3, 10
        li a4, 15
        call chacha20_quarter_round

        mv a0, s3
        li a1, 1
        li a2, 6
        li a3, 11
        li a4, 12
        call chacha20_quarter_round

        mv a0, s3
        li a1, 2
        li a2, 7
        li a3, 8
        li a4, 13
        call chacha20_quarter_round

        mv a0, s3
        li a1, 3
        li a2, 4
        li a3, 9
        li a4, 14
        call chacha20_quarter_round
        
        addi s0, s0, -1
        bnez s0, round_loop
    round_end:

    lw t0, 0(s3) #carga el working_state
    lw t1, 0(s1) #carga el state
    add t0, t0, t1 #se suma el working_state + state
    sw t0, 0(s3) #working_state = resultado
    
    lw t0, 4(s3) #carga el working_state
    lw t1, 4(s1) #carga el state
    add t0, t0, t1 #se suma el working_state + state
    sw t0, 4(s3) #working_state = resultado

    lw t0, 8(s3) #carga el working_state
    lw t1, 8(s1) #carga el state
    add t0, t0, t1 #se suma el working_state + state
    sw t0, 8(s3) #working_state = resultado

    lw t0, 12(s3) #carga el working_state
    lw t1, 12(s1) #carga el state
    add t0, t0, t1 #se suma el working_state + state
    sw t0, 12(s3) #working_state = resultado

    lw t0, 16(s3) #carga el working_state
    lw t1, 16(s1) #carga el state
    add t0, t0, t1 #se suma el working_state + state
    sw t0, 16(s3) #working_state = resultado

    lw t0, 20(s3) #carga el working_state
    lw t1, 20(s1) #carga el state
    add t0, t0, t1 #se suma el working_state + state
    sw t0, 20(s3) #working_state = resultado

    lw t0, 24(s3) #carga el working_state
    lw t1, 24(s1) #carga el state
    add t0, t0, t1 #se suma el working_state + state
    sw t0, 24(s3) #working_state = resultado

    lw t0, 28(s3) #carga el working_state
    lw t1, 28(s1) #carga el state
    add t0, t0, t1 #se suma el working_state + state
    sw t0, 28(s3) #working_state = resultado

    lw t0, 32(s3) #carga el working_state
    lw t1, 32(s1) #carga el state
    add t0, t0, t1 #se suma el working_state + state
    sw t0, 32(s3) #working_state = resultado

    lw t0, 36(s3) #carga el working_state
    lw t1, 36(s1) #carga el state
    add t0, t0, t1 #se suma el working_state + state
    sw t0, 36(s3) #working_state = resultado

    lw t0, 40(s3) #carga el working_state
    lw t1, 40(s1) #carga el state
    add t0, t0, t1 #se suma el working_state + state
    sw t0, 40(s3) #working_state = resultado

    lw t0, 44(s3) #carga el working_state
    lw t1, 44(s1) #carga el state
    add t0, t0, t1 #se suma el working_state + state
    sw t0, 44(s3) #working_state = resultado

    lw t0, 48(s3) #carga el working_state
    lw t1, 48(s1) #carga el state
    add t0, t0, t1 #se suma el working_state + state
    sw t0, 48(s3) #working_state = resultado

    lw t0, 52(s3) #carga el working_state
    lw t1, 52(s1) #carga el state
    add t0, t0, t1 #se suma el working_state + state
    sw t0, 52(s3) #working_state = resultado

    lw t0, 56(s3) #carga el working_state
    lw t1, 56(s1) #carga el state
    add t0, t0, t1 #se suma el working_state + state
    sw t0, 56(s3) #working_state = resultado

    lw t0, 60(s3) #carga el working_state
    lw t1, 60(s1) #carga el state
    add t0, t0, t1 #se suma el working_state + state
    sw t0, 60(s3) #working_state = resultado

    lw t0, 0(s3) #se cargan los valores finales
    sw t0, 0(s2) #se agregan al output (buffer de 64 bytes en main.c)

    lw t0, 4(s3) #se cargan los valores finales
    sw t0, 4(s2) #se agregan al output (buffer de 64 bytes en main.c)

    lw t0, 8(s3) #se cargan los valores finales
    sw t0, 8(s2) #se agregan al output (buffer de 64 bytes en main.c)

    lw t0, 12(s3) #se cargan los valores finales
    sw t0, 12(s2) #se agregan al output (buffer de 64 bytes en main.c)

    lw t0, 16(s3) #se cargan los valores finales
    sw t0, 16(s2) #se agregan al output (buffer de 64 bytes en main.c)

    lw t0, 20(s3) #se cargan los valores finales
    sw t0, 20(s2) #se agregan al output (buffer de 64 bytes en main.c)

    lw t0, 24(s3) #se cargan los valores finales
    sw t0, 24(s2) #se agregan al output (buffer de 64 bytes en main.c)

    lw t0, 28(s3) #se cargan los valores finales
    sw t0, 28(s2) #se agregan al output (buffer de 64 bytes en main.c)

    lw t0, 32(s3) #se cargan los valores finales
    sw t0, 32(s2) #se agregan al output (buffer de 64 bytes en main.c)

    lw t0, 36(s3) #se cargan los valores finales
    sw t0, 36(s2) #se agregan al output (buffer de 64 bytes en main.c)

    lw t0, 40(s3) #se cargan los valores finales
    sw t0, 40(s2) #se agregan al output (buffer de 64 bytes en main.c)

    lw t0, 44(s3) #se cargan los valores finales
    sw t0, 44(s2) #se agregan al output (buffer de 64 bytes en main.c)

    lw t0, 48(s3) #se cargan los valores finales
    sw t0, 48(s2) #se agregan al output (buffer de 64 bytes en main.c)

    lw t0, 52(s3) #se cargan los valores finales
    sw t0, 52(s2) #se agregan al output (buffer de 64 bytes en main.c)

    lw t0, 56(s3) #se cargan los valores finales
    sw t0, 56(s2) #se agregan al output (buffer de 64 bytes en main.c)

    lw t0, 60(s3) #se cargan los valores finales
    sw t0, 60(s2) #se agregan al output (buffer de 64 bytes en main.c)
    
    #Se crea el epilogo para finalizar la funcion (inverso del prologo)
    lw ra, 0(sp)
    lw s0, 4(sp)
    lw s1, 8(sp)
    lw s2, 12(sp)
    lw s3, 16(sp)
    addi sp, sp, 84
    ret