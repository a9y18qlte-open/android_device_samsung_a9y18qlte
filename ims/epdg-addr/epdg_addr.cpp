/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "epdg_addr"

#include <arpa/inet.h>
#include <string.h>

#include <fstream>
#include <set>
#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>
#include <netutils/ifc.h>

// "address/prefix" in inet_ntop() form, so that differently written addresses compare
// equal (Java's getHostAddress() does not compress zeros). Empty if it does not parse.
static std::string Canonical(const std::string& address) {
    std::vector<std::string> parts = android::base::Split(address, "/");
    in6_addr addr;
    unsigned prefix;
    if (parts.size() != 2 || inet_pton(AF_INET6, parts[0].c_str(), &addr) != 1 ||
        !android::base::ParseUint(parts[1], &prefix, 128u)) {
        return "";
    }
    char text[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &addr, text, sizeof(text));
    return std::string(text) + "/" + std::to_string(prefix);
}

// The global IPv6 addresses of an interface, as "address/prefix" strings.
static std::vector<std::string> GlobalAddresses(const std::string& iface) {
    std::vector<std::string> addresses;
    // <address> <ifindex> <prefix> <scope> <flags> <name>, all hex
    std::ifstream in("/proc/net/if_inet6");
    std::string hex, ifindex, prefix, scope, flags, name;
    while (in >> hex >> ifindex >> prefix >> scope >> flags >> name) {
        if (name != iface || scope != "00" || hex.size() != 32) continue;
        in6_addr addr;
        for (int i = 0; i < 16; i++) {
            addr.s6_addr[i] = std::stoul(hex.substr(i * 2, 2), nullptr, 16);
        }
        char text[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &addr, text, sizeof(text));
        addresses.push_back(std::string(text) + "/" + std::to_string(std::stoul(prefix, nullptr, 16)));
    }
    return addresses;
}

// Usage: epdg_addr "<iface> <address/prefix>..." (the value of sys.epdg.tunaddr.<iface>)
int main(int argc, char** argv) {
    android::base::InitLogging(argv, android::base::LogdLogger(android::base::SYSTEM));
    if (argc != 2) {
        LOG(ERROR) << "usage: epdg_addr \"<iface> <address/prefix>...\"";
        return 1;
    }

    std::vector<std::string> args = android::base::Split(argv[1], " ");
    // Only the ePDG tunnels (epdg<n>), never an arbitrary interface.
    if (args.size() < 2 || !android::base::StartsWith(args[0], "epdg")) {
        LOG(ERROR) << "bad argument: " << argv[1];
        return 1;
    }

    std::set<std::string> assigned;
    for (size_t i = 1; i < args.size(); i++) {
        std::string address = Canonical(args[i]);
        if (address.empty()) {
            LOG(ERROR) << "bad address: " << args[i];
            return 1;
        }
        assigned.insert(address);
    }

    // The ePDG also sends router advertisements through the tunnel, and SLAAC then
    // adds an address of its own. The IMS stack picks that one for SIP, the network
    // does not answer the protected REGISTER from it, and registration over Wi-Fi
    // times out until it falls back to LTE. Only the assigned addresses may be used.
    if (!android::base::WriteStringToFile("0", "/proc/sys/net/ipv6/conf/" + args[0] + "/autoconf")) {
        PLOG(ERROR) << "disabling autoconf on " << args[0];
    }

    int ret = 0;
    for (const auto& address : GlobalAddresses(args[0])) {
        if (assigned.count(address)) continue;
        std::vector<std::string> parts = android::base::Split(address, "/");
        int err = ifc_del_address(args[0].c_str(), parts[0].c_str(), std::stoi(parts[1]));
        if (err != 0) {
            LOG(ERROR) << "removing " << address << " from " << args[0] << ": " << strerror(-err);
            ret = 1;
        } else {
            LOG(INFO) << "removed " << address << " from " << args[0];
        }
    }

    for (const auto& address : assigned) {
        std::vector<std::string> parts = android::base::Split(address, "/");
        int err = ifc_add_address(args[0].c_str(), parts[0].c_str(), std::stoi(parts[1]));
        // EEXIST: the same address again, e.g. after a handover.
        if (err != 0 && err != -EEXIST) {
            LOG(ERROR) << "adding " << address << " to " << args[0] << ": " << strerror(-err);
            ret = 1;
        } else {
            LOG(INFO) << "added " << address << " to " << args[0];
        }
    }
    return ret;
}
