#ifndef ALG_H
#define ALG_H

#include <stdint.h>
#include <stdbool.h>

typedef struct { uint8_t face; uint8_t q; } Move; /* q in {1,2,3} CW quarter-turns */

typedef struct Alg {
    Move *m;
    int   len, cap;
} Alg;

/* Lifecycle: initialise with  Alg a = {0};  free with  alg_free(&a); */
void  alg_free(Alg *a);

/* Spec §7 Task 3.4 */
bool  alg_parse(const char *text, Alg *out);    /* SiGN notation; false on error */
char *alg_to_string(const Alg *a);              /* caller frees; single-spaced */
void  alg_invert(const Alg *in, Alg *out);      /* reverse sequence + q -> 4-q */
void  alg_concat(Alg *dst, const Alg *src);
void  alg_power_realise(const Alg *a, int m, int K, Alg *out); /* Stage 1 of D5 */
void  alg_simplify(Alg *a);                     /* Stage 2 of D5 */

#endif /* ALG_H */
