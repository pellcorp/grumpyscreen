#include "wpa_event.h"
#include "wpa_ctrl.h"
#include "config.h"
#include "logger.h"

#include <memory>
#include <experimental/filesystem>

namespace fs = std::experimental::filesystem;

WpaEvent::WpaEvent()
  : hv::EventLoopThread(NULL)
  , conn(NULL)
  , mon_conn(NULL)
  , mon_io(NULL)
{
}

WpaEvent::~WpaEvent() {
  LOG_TRACE("wpa event stopped");
  stop();
}

void WpaEvent::start() {
  if (isRunning()) {
    loop()->runInLoop(std::bind(&WpaEvent::init_wpa, this));
  } else {
    // Network startup can be triggered by an LVGL timer after the Network tab
    // has rendered its loading overlay. Never hold the UI thread waiting for
    // this worker's event loop to start.
    hv::EventLoopThread::start(false, [this]() {
      WpaEvent::init_wpa();
      return 0;
    });
  }
}

void WpaEvent::stop() {
  LOG_TRACE("wpa event stopped1");
  hv::EventLoopThread::stop(true);
}

void WpaEvent::reconnect() {
  if (!isRunning()) return;

  // Always defer this: restart_wifi() is normally called from inside the
  // monitor's read callback, where tearing down that same hio is unsafe.
  loop()->queueInLoop([this]() {
    LOG_DEBUG("reconnecting to wpa supplicant after Wi-Fi restart");
    close_connections();
    init_wpa();
  });
}

void WpaEvent::register_callback(const std::string &name,
				 std::function<void(const std::string&)> cb) {
  const auto &entry = callbacks.find(name);
  if (entry == callbacks.end()) {
    callbacks.insert({name, cb});
  } else {
    /// XXX: replace callback?
  }
}

std::string WpaEvent::find_wpa_socket() const {
  const char* p = std::getenv("WPA_SUPPLICANT_SOCKET");
  std::string wpa_socket = p ? p : "/var/run/wpa_supplicant";
  if (fs::is_directory(fs::status(wpa_socket))) {
    for (const auto &e : fs::directory_iterator(wpa_socket)) {
      if (fs::is_socket(e.path()) && e.path().string().find("p2p") == std::string::npos) {
        LOG_DEBUG("found wpa supplicant socket {}", e.path().string());
        wpa_socket = e.path().string();
        break;
      }
    }
  }
  return wpa_socket;
}

void WpaEvent::close_connections() {
  if (mon_io != NULL) {
    hio_read_stop(mon_io);
    mon_io = NULL;
  }
  if (mon_conn != NULL) {
    wpa_ctrl_detach(mon_conn);
    wpa_ctrl_close(mon_conn);
    mon_conn = NULL;
  }

  std::lock_guard<std::mutex> lock(conn_mutex);
  if (conn != NULL) {
    wpa_ctrl_close(conn);
    conn = NULL;
  }
}

void WpaEvent::schedule_reconnect() {
  if (reconnect_scheduled || !isRunning()) return;

  reconnect_scheduled = true;
  loop()->setTimeout(500, [this](hv::TimerID) {
    reconnect_scheduled = false;
    if (mon_conn == NULL) init_wpa();
  });
}

void WpaEvent::init_wpa() {
  const std::string wpa_socket = find_wpa_socket();

  {
    std::lock_guard<std::mutex> lock(conn_mutex);
    if (conn == NULL) {
      conn = wpa_ctrl_open(wpa_socket.c_str());
      if (conn == NULL) {
        LOG_TRACE("failed to open wpa control");
      }
    }
  }

  mon_conn = wpa_ctrl_open(wpa_socket.c_str());
  if (mon_conn == NULL) {
    LOG_TRACE("failed to attached to wpa supplicant");
    schedule_reconnect();
    return;
  }
  if (wpa_ctrl_attach(mon_conn) != 0) {
    LOG_TRACE("failed to attach to wpa supplicant");
    wpa_ctrl_close(mon_conn);
    mon_conn = NULL;
    schedule_reconnect();
    return;
  }
  LOG_TRACE("attached to wpa supplicant");

  int monfd = wpa_ctrl_get_fd(mon_conn);
  mon_io = hio_get(loop()->loop(), monfd);
  LOG_TRACE("set io fd {}", monfd);

  if (mon_io == NULL) {
    LOG_TRACE("failed to poll wpa supplicant monitor socket");
    wpa_ctrl_detach(mon_conn);
    wpa_ctrl_close(mon_conn);
    mon_conn = NULL;
    schedule_reconnect();
    return;
  }
  hio_set_context(mon_io, this);
  hio_setcb_read(mon_io, WpaEvent::_handle_wpa_events);
  hio_read_start(mon_io);
  LOG_TRACE("registered io read callback");

  // Re-populate consumers after reconnecting; otherwise a SCAN sent while the
  // socket was down has no completion event and the Wi-Fi panel keeps spinning.
  send_command("SCAN");
}

void WpaEvent::handle_wpa_events(void *data, int len) {
  std::string event = std::string((char*)data, len);
  LOG_TRACE("handling wpa event {}", event);
  for (const auto &entry : callbacks) {
    entry.second(event);
  }
}

std::string WpaEvent::send_command(const std::string &cmd) {
  std::lock_guard<std::mutex> lock(conn_mutex);
  char resp[4096];
  size_t len = sizeof(resp) - 1;
  if (conn != NULL) {
    LOG_TRACE("sending cmd {}", cmd);
    if (wpa_ctrl_request(conn, cmd.c_str(), cmd.length(), resp, &len, NULL) == 0) {
      return std::string(resp, len);
    }
    LOG_TRACE("failed to send cmd {} to wpa supplicant", cmd);
    wpa_ctrl_close(conn);
    conn = NULL;
  }

  // The configured Wi-Fi restart can replace supplicant's Unix socket. Open
  // the current socket path and retry once instead of requiring a UI restart.
  const std::string wpa_socket = find_wpa_socket();
  conn = wpa_ctrl_open(wpa_socket.c_str());
  if (conn != NULL) {
    len = sizeof(resp) - 1;
    LOG_TRACE("retrying cmd {} on a new wpa control connection", cmd);
    if (wpa_ctrl_request(conn, cmd.c_str(), cmd.length(), resp, &len, NULL) == 0) {
      return std::string(resp, len);
    }
    LOG_TRACE("failed to retry cmd {} to wpa supplicant", cmd);
    wpa_ctrl_close(conn);
    conn = NULL;
  }

  return "";
}
