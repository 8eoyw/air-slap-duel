// NimBLE central: keeps connections to SLAP-ATK and SLAP-DEF, subscribes to
// their event characteristic and writes clock-sync beacons.
#include "sdkconfig.h"
#if CONFIG_SLAP_MODE_BLE

#include <string.h>

#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "slap_internal.h"

static const char* TAG = "slap_ble";

namespace {

struct Peer {
  const char* name;
  uint16_t conn = BLE_HS_CONN_HANDLE_NONE;
  uint16_t svc_start = 0, svc_end = 0;
  uint16_t event_val = 0, sync_val = 0, event_cccd = 0;
  bool ready = false;
};
Peer g_peers[2] = {{SLAP_NAME_ATTACKER}, {SLAP_NAME_DEFENDER}};
uint8_t g_own_addr_type;
bool g_connecting = false;

ble_uuid_any_t g_svc_uuid, g_event_uuid, g_sync_uuid;
const ble_uuid16_t kCccdUuid = BLE_UUID16_INIT(BLE_GATT_DSC_CLT_CFG_UUID16);

void start_scan();
int gap_event(struct ble_gap_event* ev, void* arg);

int peer_by_conn(uint16_t conn) {
  for (int i = 0; i < 2; i++)
    if (g_peers[i].conn == conn) return i;
  return -1;
}

void post_peer(int idx, bool connected) {
  SlapMsg m = {};
  m.kind = SlapMsg::PEER;
  m.peer = idx;
  m.connected = connected;
  slap_post(m);
}

void drop(uint16_t conn, const char* why) {
  ESP_LOGW(TAG, "dropping conn %d: %s", conn, why);
  ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
}

// ---- discovery chain: service -> characteristics -> CCCD -> subscribe ----

int on_subscribed(uint16_t conn, const struct ble_gatt_error* err, struct ble_gatt_attr*, void*) {
  int i = peer_by_conn(conn);
  if (i < 0) return 0;
  if (err->status != 0) { drop(conn, "subscribe failed"); return 0; }
  g_peers[i].ready = true;
  ESP_LOGI(TAG, "%s ready (event=%d sync=%d)", g_peers[i].name, g_peers[i].event_val, g_peers[i].sync_val);
  post_peer(i, true);
  start_scan();  // look for the other one
  return 0;
}

int on_dsc(uint16_t conn, const struct ble_gatt_error* err, uint16_t, const struct ble_gatt_dsc* dsc, void*) {
  int i = peer_by_conn(conn);
  if (i < 0) return 0;
  Peer& p = g_peers[i];
  if (err->status == 0 && dsc) {
    // Descriptors come in handle order, so the first CCCD after the event
    // value handle belongs to the event characteristic.
    if (!p.event_cccd && ble_uuid_cmp(&dsc->uuid.u, &kCccdUuid.u) == 0) p.event_cccd = dsc->handle;
    return 0;
  }
  if (err->status == BLE_HS_EDONE) {
    if (!p.event_cccd) { drop(conn, "no CCCD"); return 0; }
    static const uint8_t on[2] = {1, 0};
    ble_gattc_write_flat(conn, p.event_cccd, on, sizeof(on), on_subscribed, nullptr);
  } else {
    drop(conn, "descriptor discovery failed");
  }
  return 0;
}

int on_chr(uint16_t conn, const struct ble_gatt_error* err, const struct ble_gatt_chr* chr, void*) {
  int i = peer_by_conn(conn);
  if (i < 0) return 0;
  Peer& p = g_peers[i];
  if (err->status == 0 && chr) {
    if (ble_uuid_cmp(&chr->uuid.u, &g_event_uuid.u) == 0) p.event_val = chr->val_handle;
    if (ble_uuid_cmp(&chr->uuid.u, &g_sync_uuid.u) == 0) p.sync_val = chr->val_handle;
    return 0;
  }
  if (err->status == BLE_HS_EDONE) {
    if (!p.event_val || !p.sync_val) { drop(conn, "characteristics missing"); return 0; }
    ble_gattc_disc_all_dscs(conn, p.event_val, p.svc_end, on_dsc, nullptr);
  } else {
    drop(conn, "characteristic discovery failed");
  }
  return 0;
}

int on_svc(uint16_t conn, const struct ble_gatt_error* err, const struct ble_gatt_svc* svc, void*) {
  int i = peer_by_conn(conn);
  if (i < 0) return 0;
  Peer& p = g_peers[i];
  if (err->status == 0 && svc) {
    p.svc_start = svc->start_handle;
    p.svc_end = svc->end_handle;
    return 0;
  }
  if (err->status == BLE_HS_EDONE && p.svc_start) {
    ble_gattc_disc_all_chrs(conn, p.svc_start, p.svc_end, on_chr, nullptr);
  } else {
    drop(conn, "service not found");
  }
  return 0;
}

// ---- GAP ----

int match_name(const struct ble_gap_disc_desc& d) {
  struct ble_hs_adv_fields f;
  if (ble_hs_adv_parse_fields(&f, d.data, d.length_data) != 0 || !f.name) return -1;
  for (int i = 0; i < 2; i++) {
    size_t n = strlen(g_peers[i].name);
    if (!g_peers[i].ready && g_peers[i].conn == BLE_HS_CONN_HANDLE_NONE && f.name_len == n &&
        memcmp(f.name, g_peers[i].name, n) == 0)
      return i;
  }
  return -1;
}

void connect_to(const struct ble_gap_disc_desc& d, int idx) {
  if (g_connecting) return;
  ble_gap_disc_cancel();
  struct ble_gap_conn_params cp = {};
  cp.scan_itvl = 0x0010;
  cp.scan_window = 0x0010;
  cp.itvl_min = 6;    // 7.5 ms
  cp.itvl_max = 12;   // 15 ms
  cp.latency = 0;
  cp.supervision_timeout = 200;  // 2 s
  cp.min_ce_len = 0;
  cp.max_ce_len = 0;
  int rc = ble_gap_connect(g_own_addr_type, &d.addr, 3000, &cp, gap_event, (void*)(intptr_t)idx);
  if (rc == 0) {
    g_connecting = true;
    ESP_LOGI(TAG, "connecting to %s", g_peers[idx].name);
  } else {
    ESP_LOGW(TAG, "connect rc=%d", rc);
    start_scan();
  }
}

int gap_event(struct ble_gap_event* ev, void* arg) {
  switch (ev->type) {
    case BLE_GAP_EVENT_DISC: {
      // ArduinoBLE puts the local name in the scan response, so scan actively.
      int idx = match_name(ev->disc);
      if (idx >= 0) connect_to(ev->disc, idx);
      return 0;
    }
    case BLE_GAP_EVENT_DISC_COMPLETE:
      start_scan();
      return 0;

    case BLE_GAP_EVENT_CONNECT: {
      g_connecting = false;
      int idx = (int)(intptr_t)arg;
      if (ev->connect.status != 0) {
        ESP_LOGW(TAG, "connect to %s failed: %d", g_peers[idx].name, ev->connect.status);
        start_scan();
        return 0;
      }
      Peer& p = g_peers[idx];
      p = Peer{p.name};
      p.conn = ev->connect.conn_handle;
      ble_gattc_disc_svc_by_uuid(p.conn, &g_svc_uuid.u, on_svc, nullptr);
      return 0;
    }
    case BLE_GAP_EVENT_DISCONNECT: {
      int idx = peer_by_conn(ev->disconnect.conn.conn_handle);
      if (idx >= 0) {
        ESP_LOGW(TAG, "%s disconnected (reason %d)", g_peers[idx].name, ev->disconnect.reason);
        bool was_ready = g_peers[idx].ready;
        g_peers[idx] = Peer{g_peers[idx].name};
        if (was_ready) post_peer(idx, false);
      }
      start_scan();
      return 0;
    }
    case BLE_GAP_EVENT_NOTIFY_RX: {
      int idx = peer_by_conn(ev->notify_rx.conn_handle);
      if (idx < 0 || ev->notify_rx.attr_handle != g_peers[idx].event_val) return 0;
      SlapMsg m = {};
      m.kind = SlapMsg::EVENT;
      m.peer = idx;
      m.rx_ms = slap_now_ms();
      if (OS_MBUF_PKTLEN(ev->notify_rx.om) < sizeof(SlapEvent)) return 0;
      os_mbuf_copydata(ev->notify_rx.om, 0, sizeof(SlapEvent), &m.event);
      slap_post(m);
      return 0;
    }
    case BLE_GAP_EVENT_CONN_UPDATE: {
      struct ble_gap_conn_desc d;
      if (ble_gap_conn_find(ev->conn_update.conn_handle, &d) == 0)
        ESP_LOGI(TAG, "conn %d interval %.2f ms", ev->conn_update.conn_handle, d.conn_itvl * 1.25f);
      return 0;
    }
    default:
      return 0;
  }
}

void start_scan() {
  if (g_connecting || ble_gap_disc_active()) return;
  if (g_peers[0].conn != BLE_HS_CONN_HANDLE_NONE && g_peers[1].conn != BLE_HS_CONN_HANDLE_NONE) return;
  struct ble_gap_disc_params dp = {};
  dp.filter_duplicates = 0;  // the scan response (with the name) comes after the adv report
  dp.passive = 0;
  int rc = ble_gap_disc(g_own_addr_type, 10000, &dp, gap_event, nullptr);
  if (rc != 0) ESP_LOGW(TAG, "scan rc=%d", rc);
}

void on_sync() {
  ble_hs_util_ensure_addr(0);
  ble_hs_id_infer_auto(0, &g_own_addr_type);
  start_scan();
}

void on_reset(int reason) { ESP_LOGE(TAG, "host reset, reason %d", reason); }

void host_task(void*) {
  nimble_port_run();
  nimble_port_freertos_deinit();
}

}  // namespace

void slap_ble_start() {
  ble_uuid_from_str(&g_svc_uuid, SLAP_SERVICE_UUID);
  ble_uuid_from_str(&g_event_uuid, SLAP_EVENT_UUID);
  ble_uuid_from_str(&g_sync_uuid, SLAP_SYNC_UUID);
  if (nimble_port_init() != ESP_OK) {
    ESP_LOGE(TAG, "nimble_port_init failed");
    return;
  }
  ble_hs_cfg.sync_cb = on_sync;
  ble_hs_cfg.reset_cb = on_reset;
  nimble_port_freertos_init(host_task);
}

void slap_ble_send_sync(uint32_t referee_ms) {
  SlapSync s = {referee_ms};
  for (Peer& p : g_peers)
    if (p.ready) ble_gattc_write_no_rsp_flat(p.conn, p.sync_val, &s, sizeof(s));
}

#endif  // CONFIG_SLAP_MODE_BLE
