#include <stdint.h>
#include <stdio.h>

#include "../ink_photo_core.c"

int main(void) {
  uint8_t palette[16 * 4] = {0};
  uint8_t map[16] = {0};
  if (!ink_photo_core_self_test()) {
    fputs("FAIL: photo core self test\n", stderr);
    return 1;
  }
  for (uint8_t i = 0; i < 16; ++i) {
    const uint8_t level = (uint8_t)(i * 17);
    palette[i * 4] = level;
    palette[i * 4 + 1] = level;
    palette[i * 4 + 2] = level;
  }

  if (!classify(palette, 16, map) || gray_for(0, map) != 3 ||
      gray_for(5, map) != 2 || gray_for(10, map) != 1 ||
      gray_for(15, map) != 0) {
    fputs("FAIL: 16-color palette was not mapped across four gray levels\n",
          stderr);
    return 1;
  }

  puts("PASS: photo palette host tests");
  return 0;
}
