#include <stdio.h>

#include "reader_refresh_policy.h"

int main(void) {
  reader_refresh_state_t state;
  reader_refresh_state_init(&state);
  if (reader_refresh_state_choose(&state, READER_REFRESH_PARTIAL) !=
      READER_REFRESH_FULL) {
    fputs("invalid screen did not force full refresh\n", stderr);
    return 1;
  }

  reader_refresh_state_record(&state, true);
  if (reader_refresh_state_choose(&state, READER_REFRESH_PARTIAL) !=
      READER_REFRESH_PARTIAL) {
    fputs("successful full refresh did not enable partial refresh\n", stderr);
    return 1;
  }

  reader_refresh_state_record(&state, false);
  if (reader_refresh_state_choose(&state, READER_REFRESH_PARTIAL) !=
      READER_REFRESH_FULL) {
    fputs("failed partial refresh did not force recovery full refresh\n",
          stderr);
    return 1;
  }

  reader_refresh_state_record(&state, false);
  if (reader_refresh_state_choose(&state, READER_REFRESH_PARTIAL) !=
      READER_REFRESH_FULL) {
    fputs("failed recovery refresh incorrectly restored screen\n", stderr);
    return 1;
  }

  reader_refresh_state_record(&state, true);
  if (reader_refresh_state_choose(&state, READER_REFRESH_PARTIAL) !=
      READER_REFRESH_PARTIAL) {
    fputs("successful recovery refresh did not restore screen\n", stderr);
    return 1;
  }

  puts("PASS: reader refresh policy host tests");
  return 0;
}
