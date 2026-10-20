// Protocol v4 BLE bridge, port of magpie-usage-display's ble_bridge.cpp
// (MIT, peterroe) from the Arduino/BLE stack to NimBLE.
#include "ble_bridge.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "cJSON.h"

static const char *TAG = "ble";

#define DEVICE_NAME "Magpie Display"
#define PROTOCOL_VERSION 4
#define STATUS_WATCHDOG_MS (45 * 1000)

// 7d8b6c20-8f6d-4b44-a0f8-1b6570c0de01 (little-endian byte order)
#define SERVICE_UUID_BYTES                                                     \
  0x01, 0xde, 0xc0, 0x70, 0x65, 0x1b, 0xf8, 0xa0, 0x44, 0x4b, 0x6d, 0x8f,     \
      0x20, 0x6c, 0x8b, 0x7d
#define STATUS_UUID_BYTES                                                      \
  0x02, 0xde, 0xc0, 0x70, 0x65, 0x1b, 0xf8, 0xa0, 0x44, 0x4b, 0x6d, 0x8f,     \
      0x20, 0x6c, 0x8b, 0x7d
#define COMMAND_UUID_BYTES                                                     \
  0x03, 0xde, 0xc0, 0x70, 0x65, 0x1b, 0xf8, 0xa0, 0x44, 0x4b, 0x6d, 0x8f,     \
      0x20, 0x6c, 0x8b, 0x7d
#define RESULT_UUID_BYTES                                                      \
  0x04, 0xde, 0xc0, 0x70, 0x65, 0x1b, 0xf8, 0xa0, 0x44, 0x4b, 0x6d, 0x8f,     \
      0x20, 0x6c, 0x8b, 0x7d
#define DEVICE_INFO_UUID_BYTES                                                 \
  0x05, 0xde, 0xc0, 0x70, 0x65, 0x1b, 0xf8, 0xa0, 0x44, 0x4b, 0x6d, 0x8f,     \
      0x20, 0x6c, 0x8b, 0x7d

static const ble_uuid128_t s_service_uuid = BLE_UUID128_INIT(SERVICE_UUID_BYTES);
static const ble_uuid128_t s_status_uuid = BLE_UUID128_INIT(STATUS_UUID_BYTES);
static const ble_uuid128_t s_command_uuid = BLE_UUID128_INIT(COMMAND_UUID_BYTES);
static const ble_uuid128_t s_result_uuid = BLE_UUID128_INIT(RESULT_UUID_BYTES);
static const ble_uuid128_t s_device_info_uuid =
    BLE_UUID128_INIT(DEVICE_INFO_UUID_BYTES);

static uint8_t s_own_addr_type;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_command_val_handle;
static QueueHandle_t s_state_queue;
static QueueHandle_t s_result_queue;
static QueueHandle_t s_pairing_code_queue;
static QueueHandle_t s_pairing_finished_queue;
static volatile bool s_connected;
static volatile bool s_status_received;
static uint32_t s_last_status_at_ms;
static uint32_t s_next_request_id = 1;
static uint32_t s_command_session_id;
static uint32_t s_last_sequence;
static bool s_has_bond;
static bool s_pairing_announced;
static char s_command_payload[96];

static void advertise(void);

static uint32_t now_ms(void) {
  return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static bool json_uint(cJSON *root, const char *key, uint64_t *out) {
  cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
  if (!cJSON_IsNumber(item) || item->valuedouble < 0) {
    return false;
  }
  *out = (uint64_t)item->valuedouble;
  return true;
}

static void handle_status_write(const uint8_t *data, size_t len) {
  if (len == 0 || len > 256) {
    ESP_LOGW(TAG, "rejected invalid status length %u", (unsigned)len);
    return;
  }

  char *text = malloc(len + 1);
  if (text == NULL) {
    return;
  }
  memcpy(text, data, len);
  text[len] = '\0';

  cJSON *root = cJSON_Parse(text);
  free(text);
  if (root == NULL) {
    ESP_LOGW(TAG, "rejected invalid status JSON");
    return;
  }

  uint64_t version = 0;
  if (!json_uint(root, "v", &version) || version != PROTOCOL_VERSION) {
    ESP_LOGW(TAG, "rejected unsupported protocol version");
    cJSON_Delete(root);
    return;
  }

  uint64_t sequence = 0;
  json_uint(root, "s", &sequence);
  if (sequence == 0 || (s_last_sequence != 0 && sequence < s_last_sequence)) {
    ESP_LOGW(TAG, "rejected stale status seq=%llu",
             (unsigned long long)sequence);
    cJSON_Delete(root);
    return;
  }

  app_state_t state = {0};
  uint64_t value = 0;
  json_uint(root, "t", &value);
  state.unix_time = value;
  json_uint(root, "o", &value);
  state.utc_offset_minutes = (int16_t)value;
  json_uint(root, "r", &value);
  state.remaining_percent = value > 100 ? 100 : (uint8_t)value;
  state.quota_reset_seconds = 0;
  json_uint(root, "d", &value);
  state.tokens_today = value;
  json_uint(root, "w", &value);
  state.tokens_7d = value;
  json_uint(root, "f", &value);
  state.calls_today = (uint32_t)value;
  json_uint(root, "z", &value);
  state.errors_today = (uint32_t)value;
  json_uint(root, "y", &value);
  state.success_permille = value > 1000 ? 1000 : (uint16_t)value;
  json_uint(root, "l", &value);
  state.average_ttft_ms = (uint32_t)value;

  cJSON *model = cJSON_GetObjectItemCaseSensitive(root, "m");
  if (cJSON_IsString(model) && model->valuestring != NULL) {
    strncpy(state.latest_model, model->valuestring,
            sizeof(state.latest_model) - 1);
  } else {
    strncpy(state.latest_model, "-", sizeof(state.latest_model) - 1);
  }
  json_uint(root, "h", &value);
  state.latest_status = (uint16_t)value;
  json_uint(root, "n", &value);
  state.latest_latency_ms = (uint32_t)value;
  json_uint(root, "b", &value);
  state.latest_failed = value == 1;

  cJSON *extended = cJSON_GetObjectItemCaseSensitive(root, "x");
  if (cJSON_IsArray(extended)) {
    const int count = cJSON_GetArraySize(extended);
    uint64_t values[10] = {0};
    for (int i = 0; i < count && i < 10; ++i) {
      cJSON *item = cJSON_GetArrayItem(extended, i);
      if (cJSON_IsNumber(item)) {
        values[i] = (uint64_t)item->valuedouble;
      } else if (cJSON_IsString(item) && i == 8) {
        strncpy(state.top_model, item->valuestring, sizeof(state.top_model) - 1);
        continue;
      }
    }
    state.session_count = (uint16_t)values[0];
    state.total_calls = (uint32_t)values[1];
    state.total_input = values[2];
    state.total_output = values[3];
    state.total_cache_read = values[4];
    state.total_reasoning = values[5];
    state.total_cost_micros = (uint32_t)values[6];
    state.cache_hit_permille = values[7] > 1000 ? 1000 : (uint16_t)values[7];
    if (state.top_model[0] == '\0') {
      strncpy(state.top_model, "-", sizeof(state.top_model) - 1);
    }
    state.top_model_calls = (uint32_t)values[9];
  }
  cJSON_Delete(root);

  state.connection = CONN_LINKED;
  state.time_valid = state.unix_time > 0;
  state.data_valid = true;

  s_last_sequence = (uint32_t)sequence;
  s_last_status_at_ms = now_ms();
  s_status_received = true;
  xQueueOverwrite(s_state_queue, &state);
  ESP_LOGI(TAG, "status seq=%llu model=%s", (unsigned long long)sequence,
           state.latest_model);
}

static void handle_result_write(const uint8_t *data, size_t len) {
  cJSON *root = cJSON_ParseWithLength((const char *)data, len);
  if (root == NULL) {
    return;
  }
  uint64_t version = 0;
  if (!json_uint(root, "v", &version) || version != PROTOCOL_VERSION) {
    cJSON_Delete(root);
    return;
  }
  ble_action_result_t result = {0};
  uint64_t ok = 0;
  json_uint(root, "ok", &ok);
  cJSON *message = cJSON_GetObjectItemCaseSensitive(root, "m");
  strncpy(result.message,
          (cJSON_IsString(message) && message->valuestring != NULL)
              ? message->valuestring
              : (ok == 1 ? "DONE" : "FAILED"),
          sizeof(result.message) - 1);
  xQueueOverwrite(s_result_queue, &result);
  cJSON_Delete(root);
}

static int gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                       struct ble_gatt_access_ctxt *ctxt, void *arg) {
  const ble_uuid_t *uuid = ctxt->chr->uuid;
  (void)conn_handle;
  (void)attr_handle;

  if (ble_uuid_cmp(uuid, &s_status_uuid.u) == 0 &&
      ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
    uint8_t buffer[256];
    const uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len > sizeof(buffer)) {
      return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    uint16_t copied = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, buffer, sizeof(buffer), &copied) != 0) {
      return BLE_ATT_ERR_UNLIKELY;
    }
    handle_status_write(buffer, copied);
    return 0;
  }

  if (ble_uuid_cmp(uuid, &s_result_uuid.u) == 0 &&
      ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
    uint8_t buffer[160];
    const uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len > sizeof(buffer)) {
      return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    uint16_t copied = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, buffer, sizeof(buffer), &copied) != 0) {
      return BLE_ATT_ERR_UNLIKELY;
    }
    handle_result_write(buffer, copied);
    return 0;
  }

  if (ble_uuid_cmp(uuid, &s_command_uuid.u) == 0 &&
      ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
    const uint16_t len = strlen(s_command_payload);
    const int rc = os_mbuf_append(ctxt->om, s_command_payload, len);
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
  }

  if (ble_uuid_cmp(uuid, &s_device_info_uuid.u) == 0 &&
      ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
    const char *info = "{\"v\":4,\"fw\":\"0.4.0-mosaico\",\"screen\":\"480x480\"}";
    const int rc = os_mbuf_append(ctxt->om, info, strlen(info));
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
  }

  return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_svc_def s_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_service_uuid.u,
        .characteristics =
            (struct ble_gatt_chr_def[]){
                {
                    .uuid = &s_status_uuid.u,
                    .access_cb = gatt_access,
                    .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC,
                },
                {
                    .uuid = &s_command_uuid.u,
                    .access_cb = gatt_access,
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC |
                             BLE_GATT_CHR_F_NOTIFY,
                    .val_handle = &s_command_val_handle,
                },
                {
                    .uuid = &s_result_uuid.u,
                    .access_cb = gatt_access,
                    .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC,
                },
                {
                    .uuid = &s_device_info_uuid.u,
                    .access_cb = gatt_access,
                    .flags = BLE_GATT_CHR_F_READ,
                },
                {0},
            },
    },
    {0},
};

static int gap_event(struct ble_gap_event *event, void *arg) {
  switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
      if (event->connect.status == 0) {
        s_conn_handle = event->connect.conn_handle;
        s_connected = true;
        s_status_received = false;
        s_last_sequence = 0;
        ESP_LOGI(TAG, "connected");
        // macOS will not start pairing on its own just because the peer has
        // encrypted-only characteristics: it reports "Encryption is
        // insufficient" and gives up. Kick off security here so the passkey
        // dialog appears on the Mac and the code shows on our screen.
        ble_gap_security_initiate(s_conn_handle);
      } else {
        advertise();
      }
      return 0;

    case BLE_GAP_EVENT_DISCONNECT:
      ESP_LOGI(TAG, "disconnected reason=%d", event->disconnect.reason);
      s_connected = false;
      s_status_received = false;
      s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
      advertise();
      return 0;

    case BLE_GAP_EVENT_PASSKEY_ACTION: {
      struct ble_sm_io io = {0};
      if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
        io.action = BLE_SM_IOACT_DISP;
        io.passkey = (uint32_t)(esp_random() % 1000000U);
        if (s_has_bond) {
          ESP_LOGI(TAG, "bonded reconnect, passkey UI suppressed");
        } else {
          s_pairing_announced = true;
          xQueueOverwrite(s_pairing_code_queue, &io.passkey);
        }
        ESP_LOGI(TAG, "pairing passkey %06lu", (unsigned long)io.passkey);
        ble_sm_inject_io(event->passkey.conn_handle, &io);
      }
      return 0;
    }

    case BLE_GAP_EVENT_ENC_CHANGE: {
      struct ble_gap_conn_desc desc;
      const bool success =
          event->enc_change.status == 0 &&
          ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0 &&
          desc.sec_state.encrypted;
      if (success) {
        s_has_bond = true;
      }
      if (s_pairing_announced) {
        xQueueOverwrite(s_pairing_finished_queue, &success);
        s_pairing_announced = false;
      }
      ESP_LOGI(TAG, "encryption change status=%d", event->enc_change.status);
      return 0;
    }

    case BLE_GAP_EVENT_REPEAT_PAIRING: {
      // The Mac dropped its bond; forget ours and pair again.
      struct ble_gap_conn_desc desc;
      if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
        ble_store_util_delete_peer(&desc.peer_id_addr);
      }
      return BLE_GAP_REPEAT_PAIRING_RETRY;
    }

    case BLE_GAP_EVENT_ADV_COMPLETE:
      advertise();
      return 0;

    case BLE_GAP_EVENT_MTU:
      ESP_LOGI(TAG, "mtu updated to %d", event->mtu.value);
      return 0;

    default:
      return 0;
  }
}

static void advertise(void) {
  struct ble_hs_adv_fields fields = {0};
  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  fields.uuids128 = (ble_uuid128_t *)&s_service_uuid;
  fields.num_uuids128 = 1;
  fields.uuids128_is_complete = 1;
  int rc = ble_gap_adv_set_fields(&fields);
  if (rc != 0) {
    ESP_LOGE(TAG, "adv fields failed rc=%d", rc);
    return;
  }

  struct ble_hs_adv_fields rsp = {0};
  rsp.name = (const uint8_t *)DEVICE_NAME;
  rsp.name_len = strlen(DEVICE_NAME);
  rsp.name_is_complete = 1;
  rc = ble_gap_adv_rsp_set_fields(&rsp);
  if (rc != 0) {
    ESP_LOGE(TAG, "scan rsp failed rc=%d", rc);
  }

  struct ble_gap_adv_params params = {0};
  params.conn_mode = BLE_GAP_CONN_MODE_UND;
  params.disc_mode = BLE_GAP_DISC_MODE_GEN;
  rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &params,
                         gap_event, NULL);
  if (rc != 0 && rc != BLE_HS_EALREADY) {
    ESP_LOGE(TAG, "adv start failed rc=%d", rc);
  } else {
    ESP_LOGI(TAG, "advertising as \"%s\"", DEVICE_NAME);
  }
}

static void on_sync(void) {
  int rc = ble_hs_util_ensure_addr(0);
  if (rc == 0) {
    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
  }
  if (rc != 0) {
    ESP_LOGE(TAG, "address setup failed rc=%d", rc);
    return;
  }
  advertise();
}

static void on_reset(int reason) {
  ESP_LOGW(TAG, "host reset reason=%d", reason);
}

static void host_task(void *param) {
  (void)param;
  nimble_port_run();
  nimble_port_freertos_deinit();
}

bool ble_begin(void) {
  s_state_queue = xQueueCreate(1, sizeof(app_state_t));
  s_result_queue = xQueueCreate(1, sizeof(ble_action_result_t));
  s_pairing_code_queue = xQueueCreate(1, sizeof(uint32_t));
  s_pairing_finished_queue = xQueueCreate(1, sizeof(bool));
  if (s_state_queue == NULL || s_result_queue == NULL ||
      s_pairing_code_queue == NULL || s_pairing_finished_queue == NULL) {
    ESP_LOGE(TAG, "queue allocation failed");
    return false;
  }

  s_command_session_id = esp_random();
  if (s_command_session_id == 0) {
    s_command_session_id = 1;
  }
  s_has_bond = false;

  esp_err_t err = nimble_port_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(err));
    return false;
  }

  // The companion writes ~250-byte status snapshots; ask for a 256-byte MTU.
  ble_att_set_preferred_mtu(256);

  ble_hs_cfg.sync_cb = on_sync;
  ble_hs_cfg.reset_cb = on_reset;
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
  // SC-only mode requires MITM authentication in this NimBLE version.
  // Display the passkey on the panel; the Mac supplies keyboard input.
  ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_DISP_ONLY;
  ble_hs_cfg.sm_bonding = 1;
  ble_hs_cfg.sm_mitm = 1;
  ble_hs_cfg.sm_sc = 1;
  ble_hs_cfg.sm_sc_only = 1;
  ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

  ble_svc_gap_init();
  ble_svc_gatt_init();
  int rc = ble_gatts_count_cfg(s_services);
  if (rc == 0) {
    rc = ble_gatts_add_svcs(s_services);
  }
  if (rc != 0) {
    ESP_LOGE(TAG, "GATT registration failed rc=%d", rc);
    return false;
  }
  rc = ble_svc_gap_device_name_set(DEVICE_NAME);
  if (rc != 0) {
    ESP_LOGW(TAG, "device name set failed rc=%d", rc);
  }

  nimble_port_freertos_init(host_task);
  return true;
}

bool ble_connected(void) { return s_connected; }

void ble_maintain_connection(void) {
  if (!s_connected || !s_status_received ||
      s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
    return;
  }
  if (now_ms() - s_last_status_at_ms < STATUS_WATCHDOG_MS) {
    return;
  }
  s_status_received = false;
  ESP_LOGW(TAG, "status watchdog expired; disconnecting stale client");
  ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
}

void ble_start_advertising(void) {
  if (!s_connected) {
    advertise();
  }
}

bool ble_take_state(app_state_t *state) {
  return s_state_queue != NULL &&
         xQueueReceive(s_state_queue, state, 0) == pdTRUE;
}

bool ble_take_action_result(ble_action_result_t *result) {
  return s_result_queue != NULL &&
         xQueueReceive(s_result_queue, result, 0) == pdTRUE;
}

bool ble_take_pairing_code(uint32_t *passkey) {
  return s_pairing_code_queue != NULL &&
         xQueueReceive(s_pairing_code_queue, passkey, 0) == pdTRUE;
}

bool ble_take_pairing_finished(bool *success) {
  return s_pairing_finished_queue != NULL &&
         xQueueReceive(s_pairing_finished_queue, success, 0) == pdTRUE;
}

bool ble_send_action(const char *action) {
  if (!s_connected || s_command_val_handle == 0) {
    return false;
  }

  const uint32_t request_id = s_next_request_id++;
  const int len = snprintf(s_command_payload, sizeof(s_command_payload),
                           "{\"v\":%d,\"sid\":%lu,\"id\":%lu,\"a\":\"%s\"}",
                           PROTOCOL_VERSION,
                           (unsigned long)s_command_session_id,
                           (unsigned long)request_id, action);
  if (len <= 0) {
    return false;
  }

  struct os_mbuf *om = ble_hs_mbuf_from_flat(s_command_payload, len);
  if (om == NULL) {
    return false;
  }
  const int rc = ble_gatts_notify_custom(s_conn_handle, s_command_val_handle, om);
  if (rc != 0) {
    ESP_LOGW(TAG, "notify failed rc=%d", rc);
    return false;
  }
  ESP_LOGI(TAG, "action sent: %s", action);
  return true;
}

bool ble_clear_bonds(void) {
  const int rc = ble_store_clear();
  if (rc != 0) {
    ESP_LOGW(TAG, "clear bonds failed rc=%d", rc);
    return false;
  }
  s_has_bond = false;
  return true;
}
