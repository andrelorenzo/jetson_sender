#include "app_support.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>

#include <cstdio>
#include <cstring>

#include "logger.h"

namespace rsapp {

std::string DetectLocalIpv4() {
    struct ifaddrs *ifaddr = nullptr;
    if (getifaddrs(&ifaddr) != 0 || ifaddr == nullptr) {
        return "127.0.0.1";
    }

    std::string selected = "127.0.0.1";
    char ip[INET_ADDRSTRLEN] = {};

    for (struct ifaddrs *ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr || ifa->ifa_addr->sa_family != AF_INET) {
            continue;
        }
        if ((ifa->ifa_flags & IFF_LOOPBACK) != 0) {
            continue;
        }
        const void *addr = &reinterpret_cast<struct sockaddr_in *>(ifa->ifa_addr)->sin_addr;
        if (inet_ntop(AF_INET, addr, ip, sizeof(ip)) != nullptr) {
            selected = ip;
            break;
        }
    }

    freeifaddrs(ifaddr);
    return selected;
}

} // namespace rsapp

void printOut(verb_e verbosity, const char *msg, size_t size) {
    const char *color = "";
    const char *tag = "";

    switch (verbosity) {
        case DEBUG: color = "\x1b[90m"; tag = "DEBUG"; break;
        case INFO:  color = "\x1b[32m"; tag = "INFO "; break;
        case WARN:  color = "\x1b[33m"; tag = "WARN "; break;
        case ERROR: color = "\x1b[31m"; tag = "ERROR"; break;
        case FATAL: color = "\x1b[1;31m"; tag = "FATAL"; break;
        case NONE: return;
        default:    color = "\x1b[0m";  tag = "LOG  "; break;
    }

    std::printf("%s[%s] %.*s\x1b[0m\n", color, tag, static_cast<int>(size), msg);
    std::fflush(stdout);
}
