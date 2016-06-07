#include <stdint.h>
#include <vector>
#include <bitset>

int a[256] __attribute__((aligned(0x20)));
int b[256] __attribute__((aligned(0x20)));
int c[256] __attribute__((aligned(0x20)));

uint8_t d[256] __attribute__((aligned(0x20)));
uint8_t e[256] __attribute__((aligned(0x20)));
uint8_t f[256] __attribute__((aligned(0x20)));

bool t[256] __attribute__((aligned(0x20)));
//std::vector<bool> t __attribute__((aligned(0x20)));
//std::bitset<256> t __attribute__((aligned(0x20)));

int main () {
  int i;

#if 0
  for (i=0; i<256; i++){
    a[i] = b[i] + c[i];
  }
  for (i=0; i<32; i++){
    uint8_t temp = 63 - d[i];
    //d[i] = (e[i] > f[i]) ? e[i] : f[i];
    //d[i] = (e[i] > f[i]) ? d[i] : temp;
    d[i] = (f[i]) ? d[i] : temp;
  }
    //e[i] = (f[i]) ? e[i] : ((e[i] != 255) ? 0 : 255) ? d[i] : temp;
  for (i=0; i<32; i++){
    //uint8_t temp1 = (f[i]) ? 255 : 0;
    //uint8_t temp2 = (e[i] != 255) ? 255 : 0;
    //uint8_t temp3 = temp1 & temp2;
    uint8_t temp4 = 63 - e[i];
    e[i] = (f[i] && (e[i] != 255)) ? e[i] : temp4;
    //e[i] = ((e[i] != 255) ? 0 : 255) ? d[i] : temp;
    //d[i] = (e[i] > f[i]) ? d[i] : (63 - d[i]);
    //t[i] = (e[i] > f[i]);
    //if (t[i]) {
    /*
    if (e[i] > f[i]) {
      d[i] = 63 - d[i];
    }
    */
  }
  for (i=0; i<32; i++) {
    std::swap(e[i], f[i]);
  }
  for (i=0; i<32; i++) {
    f[i] = 1;
  }
  for (i=0; i<32; i++) {
    uint8_t temp = e[i] ^ 007;
    e[i] = (d[i] & 004) ? e[i] : temp;
  }
  for (i=0; i<32; i++) {
    uint8_t temp = e[i] ^ 070;
    e[i] = (d[i] & 040) ? e[i] : temp;
  }
  for (i=0; i<16; i++) {
    f[i] = (a[i] & 0700) >> 6;
  }
#endif

  /* SIMD binary search for 32 numbers in a[], comparing them to 64
   * sorted numbers in b[], and setting d[] to the index found.
   */

  for (i=0; i<32; i++) {
    //d[i] = (a[i] > b[32]) ? 32 : 0;
    d[i] = 0;
  }
  for (int j=4; j>=0; j--) {
    for (i=0; i<32; i++) {
      d[i] |= (a[i] > b[d[i] | (1 << j)]) ? (1 << j) : 0;
    }
  }
}
