#include "hv/requests.h"
#include "hv/hurl.h"
#include "config.h"
#include "state.h"
#include "logger.h"
#include "platform.h"

#include <cmath>
#include <time.h>
#include <sstream>
#include <iomanip>
#include <sys/ioctl.h>
#include <linux/if.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <experimental/filesystem>
#include <regex>

namespace fs = std::experimental::filesystem;

namespace KUtils {
  std::string get_root_path(const std::string root_name) {
    auto roots = State::get_instance()->get_data("/roots"_json_pointer);
    json filtered;
    std::copy_if(roots.begin(), roots.end(),
		 std::back_inserter(filtered), [&root_name](const json& item) {
		   return item.contains("name") && item["name"] == root_name;
		 });

    LOG_TRACE("roots {}, filtered {}", roots.dump(), filtered.dump());
    if (!filtered.empty()) {
      return filtered["/0/path"_json_pointer];
    }

    return "";
  }

  std::pair<std::string, size_t> get_thumbnail(const std::string &gcode_file, json &j, double scale) {
    auto &thumbs = j["/result/thumbnails"_json_pointer];
    if (!thumbs.is_null() && !thumbs.empty()) {
      // assume square, look for closest to 300x300
      auto scaled_width = scale * 300;
      LOG_DEBUG("using thumb at scaled width {}", scaled_width);
      uint32_t closest_index = 0;
      size_t thumb_width = 0;
      auto width = thumbs.at(0)["width"].is_number()
	        ? thumbs.at(0)["width"].template get<int>()
	        : std::stoi(thumbs.at(0)["width"].template get<std::string>());
      int closest = std::abs(scaled_width - width);
      for (int i = 0; i < thumbs.size(); i++) {
	      width = thumbs.at(i)["width"].is_number()
	        ? thumbs.at(i)["width"].template get<int>()
	        : std::stoi(thumbs.at(i)["width"].template get<std::string>());
	      int cur_diff = std::abs(scaled_width - width);
        if (cur_diff < closest) {
          closest = cur_diff;
          closest_index = i;
          thumb_width = width;
        }
      }

      auto &thumb = thumbs.at(closest_index);
      LOG_DEBUG("using thumb at index {}, {}", closest_index, thumbs.dump());

      // metadata thumbnail paths are relative to the current gcode file directory
      std::string relative_path = thumb["relative_path"].template get<std::string>();
      size_t found = gcode_file.find_last_of("/\\");
      if (found != std::string::npos) {
	      relative_path = gcode_file.substr(0, found + 1) + relative_path;
      }

      Config *conf = Config::get_instance();
      std::string moonraker_host = conf->get<std::string>("/moonraker/host");
      std::string fname = relative_path.substr(relative_path.find_last_of("/\\") + 1);

      // download thumbnail
      auto gcode_root = get_root_path("gcodes");
      std::string fullpath = fmt::format("{}/{}", gcode_root, relative_path);

      return std::make_pair(fullpath, thumb_width);
    }
    return std::make_pair("", 0);
  }

  std::vector<std::string> get_interfaces() {
    std::vector<std::string> ifaces;
    struct ifaddrs *addrs;
    getifaddrs(&addrs);
    for (struct ifaddrs *addr = addrs; addr != nullptr; addr = addr->ifa_next) {
      if (addr->ifa_addr && addr->ifa_addr->sa_family == AF_PACKET) {
        ifaces.push_back(addr->ifa_name);
      }
    }

    freeifaddrs(addrs);
    return ifaces;
  }

  std::string interface_ip(const std::string &interface) {
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);

    struct ifreq ifr{};
    strcpy(ifr.ifr_name, interface.c_str());
    ioctl(fd, SIOCGIFADDR, &ifr);
    close(fd);

    char ip[INET_ADDRSTRLEN];
    strcpy(ip, inet_ntoa(((sockaddr_in *) &ifr.ifr_addr)->sin_addr));
    return ip;
  }

  std::string get_wifi_interface() {
    std::string wpa_socket = std::string(std::getenv("WPA_SUPPLICANT_SOCKET"));
    if (fs::is_directory(fs::status(wpa_socket))) {
      for (const auto &e : fs::directory_iterator(wpa_socket)) {
        if (fs::is_socket(e.path()) && e.path().string().find("p2p") == std::string::npos) {
          return e.path().filename().string();
        }
      }
    }
    return "";
  }

  template <typename Out>
  void split(const std::string &s, char delim, Out result) {
    std::istringstream iss(s);
    std::string item;
    while (std::getline(iss, item, delim)) {
      *result++ = item;
    }
  }

  std::vector<std::string> split(const std::string &s, char delim) {
    std::vector<std::string> elems;
    split(s, delim, std::back_inserter(elems));
    return elems;
  }

  std::string get_obj_name(const std::string &id) {
    size_t pos = id.find_last_of(' ');
    return id.substr(pos + 1);
  }

  std::string to_title(std::string s) {
    bool last = true;
    for (char& c : s) {
      c = last ? std::toupper(c) : std::tolower(c);
      if (c == '_') {
	      c = ' ';
      }
      last = std::isspace(c);
    }
    return s;
  }

  std::string eta_string(int64_t s) {
    time_t seconds (s);
    tm p;
    gmtime_r (&seconds, &p);

    std::ostringstream os;

    if (p.tm_yday > 0)
      os << p.tm_yday << "d ";

    if (p.tm_hour > 0)
      os << p.tm_hour << "h ";

    if (p.tm_min > 0)
      os << p.tm_min << "m ";

    os << p.tm_sec << "s";
    
    return os.str();
  }

  size_t bytes_to_mb(size_t s) {
    return s / 1024 / 1024;
  }
}  // namespace KUtils
