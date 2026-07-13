#include <stdint.h>
#include <stdio.h>

#include "../ink_photo_core.c"

typedef struct {
  const ink_photo_catalog_t *catalog;
  bool decodable[4];
  size_t calls;
} decode_policy_fixture_t;

static bool fixture_try_item(const ink_photo_item_t *item, void *context) {
  decode_policy_fixture_t *fixture = context;
  const size_t index = (size_t)(item - fixture->catalog->items);
  ++fixture->calls;
  return index < 4 && fixture->decodable[index];
}

static int test_decodable_catalog_policy(void) {
  ink_photo_catalog_t catalog = {0};
  decode_policy_fixture_t fixture = {.catalog = &catalog};
  size_t selected = 99;
  if (ink_photo_catalog_find_decodable(&catalog, 0, 1, fixture_try_item,
                                       &fixture, &selected) ||
      fixture.calls != 0 || selected != 99)
    return 1;

  catalog.count = 3;
  fixture.decodable[2] = true;
  if (!ink_photo_catalog_find_decodable(&catalog, 0, 1, fixture_try_item,
                                        &fixture, &selected) ||
      selected != 2 || fixture.calls != 3)
    return 2;

  fixture.calls = 0;
  fixture.decodable[2] = false;
  selected = 99;
  if (ink_photo_catalog_find_decodable(&catalog, 1, 1, fixture_try_item,
                                       &fixture, &selected) ||
      fixture.calls != 3 || selected != 99)
    return 3;

  fixture.calls = 0;
  fixture.decodable[0] = true;
  if (!ink_photo_catalog_find_decodable(&catalog, 2, 1, fixture_try_item,
                                        &fixture, &selected) ||
      selected != 0 || fixture.calls != 2)
    return 4;

  fixture.calls = 0;
  fixture.decodable[0] = false;
  fixture.decodable[2] = true;
  if (!ink_photo_catalog_find_decodable(&catalog, 0, -1, fixture_try_item,
                                        &fixture, &selected) ||
      selected != 2 || fixture.calls != 2)
    return 5;
  return 0;
}

int main(void) {
  uint8_t palette[16 * 4] = {0};
  uint8_t map[16] = {0};
  if (!ink_photo_core_self_test()) {
    fputs("FAIL: photo core self test\n", stderr);
    return 1;
  }
  const int policy_result = test_decodable_catalog_policy();
  if (policy_result != 0) {
    fprintf(stderr, "FAIL: decodable catalog policy case=%d\n", policy_result);
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
