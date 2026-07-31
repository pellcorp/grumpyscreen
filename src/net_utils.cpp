#include "utils.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cstdlib>
#include <cstring>
#include <experimental/filesystem>
#include <ifaddrs.h>
#include <linux/if.h>
#include <sstream>
#include <sys/ioctl.h>
#include <unistd.h>

namespace fs = std::experimental::filesystem;

namespace KUtils {
  std::vector<std::string> get_interfaces() {
    std::vector<std::string> ifaces;
    struct ifaddrs *addrs;
    getifaddrs(&addrs);
    for (struct ifaddrs *addr = addrs; addr != nullptr; addr = addr->ifa_next) {
      if (addr->ifa_addr && addr->ifa_addr->sa_family == AF_PACKET) {
        const std::string iface = addr->ifa_name;
        if (!iface.empty() && (iface[0] == 'w' || iface[0] == 'e') &&
            std::find(ifaces.begin(), ifaces.end(), iface) == ifaces.end()) {
          ifaces.push_back(iface);
        }
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
    strcpy(ip, inet_ntoa(((sockaddr_in *)&ifr.ifr_addr)->sin_addr));
    return ip;
  }

  std::string get_wifi_interface() {
    const char *p = std::getenv("WPA_SUPPLICANT_SOCKET");
    std::string wpa_socket = p ? p : "/var/run/wpa_supplicant";
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
}  // namespace KUtils
