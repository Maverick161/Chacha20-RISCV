#ifndef CHACHA20_H
#define CHACHA20_H

typedef unsigned int  uint32_t;
typedef unsigned char uint8_t;

void chacha20_quarter_round(uint32_t *state, int a, int b, int c, int d);
void chacha20_block(uint32_t *state, uint8_t *output);

#endif