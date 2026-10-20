#include "app_state.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

void format_compact_tokens(uint64_t tokens, char *out, size_t out_size) {
  if (tokens >= 1000000000000ULL) {
    const double trillions = (double)tokens / 1000000000000.0;
    const int decimals = trillions >= 100 ? 0 : (trillions >= 10 ? 1 : 2);
    snprintf(out, out_size, "%.*fT", decimals, trillions);
    return;
  }
  if (tokens >= 1000000000ULL) {
    const double billions = (double)tokens / 1000000000.0;
    const int decimals = billions >= 100 ? 0 : (billions >= 10 ? 1 : 2);
    snprintf(out, out_size, "%.*fB", decimals, billions);
    return;
  }
  if (tokens >= 1000000ULL) {
    const double millions = (double)tokens / 1000000.0;
    const int decimals = millions >= 100 ? 0 : (millions >= 10 ? 1 : 2);
    snprintf(out, out_size, "%.*fM", decimals, millions);
    return;
  }
  if (tokens >= 1000ULL) {
    snprintf(out, out_size, "%.1fK", (double)tokens / 1000.0);
    return;
  }
  snprintf(out, out_size, "%llu", (unsigned long long)tokens);
}

void format_countdown(uint32_t seconds, char *out, size_t out_size) {
  const uint32_t days = seconds / 86400UL;
  const uint32_t hours = (seconds % 86400UL) / 3600UL;
  const uint32_t minutes = (seconds % 3600UL) / 60UL;
  if (days > 0) {
    snprintf(out, out_size, "%lud %luh", (unsigned long)days,
             (unsigned long)hours);
  } else if (hours > 0) {
    snprintf(out, out_size, "%luh %lum", (unsigned long)hours,
             (unsigned long)minutes);
  } else {
    snprintf(out, out_size, "%lum", (unsigned long)minutes);
  }
}

void format_datetime(const app_state_t *state, uint32_t elapsed_seconds,
                     char *out, size_t out_size) {
  if (!state->time_valid) {
    snprintf(out, out_size, "--/-- --:--");
    return;
  }
  const time_t local_time =
      (time_t)(state->unix_time + elapsed_seconds +
               (int64_t)state->utc_offset_minutes * 60LL);
  struct tm value;
  gmtime_r(&local_time, &value);
  strftime(out, out_size, "%m/%d %H:%M", &value);
}

const char *connection_label(connection_state_t state) {
  switch (state) {
    case CONN_DISCONNECTED:
      return "OFFLINE";
    case CONN_SYNCING:
      return "SYNCING";
    case CONN_LINKED:
      return "LINKED";
    case CONN_STALE:
      return "STALE";
  }
  return "OFFLINE";
}
