#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
  CONN_DISCONNECTED,
  CONN_SYNCING,
  CONN_LINKED,
  CONN_STALE,
} connection_state_t;

typedef struct {
  uint8_t remaining_percent;
  uint64_t tokens_today;
  uint64_t tokens_7d;
  uint32_t calls_today;
  uint32_t errors_today;
  uint32_t quota_reset_seconds;
  uint16_t success_permille;
  uint32_t average_ttft_ms;
  char latest_model[32];
  uint16_t latest_status;
  uint32_t latest_latency_ms;
  bool latest_failed;
  uint16_t session_count;
  uint32_t total_calls;
  uint64_t total_input;
  uint64_t total_output;
  uint64_t total_cache_read;
  uint64_t total_reasoning;
  uint32_t total_cost_micros;
  uint16_t cache_hit_permille;
  char top_model[24];
  uint32_t top_model_calls;

  uint64_t unix_time;
  int16_t utc_offset_minutes;
  connection_state_t connection;
  bool time_valid;
  bool data_valid;
} app_state_t;

// Formats a token count as e.g. "12.3M". Buffer must hold >= 16 bytes.
void format_compact_tokens(uint64_t tokens, char *out, size_t out_size);
// Formats seconds as "2d 5h" / "5h 12m" / "7m". Buffer >= 16 bytes.
void format_countdown(uint32_t seconds, char *out, size_t out_size);
// Formats the snapshot time as "MM/DD HH:MM". Buffer >= 16 bytes.
void format_datetime(const app_state_t *state, uint32_t elapsed_seconds,
                     char *out, size_t out_size);
const char *connection_label(connection_state_t state);
