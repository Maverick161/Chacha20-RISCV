.section .text
.globl chacha20_quarter_round

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



