// FolixAgent: a tiny background helper (LaunchAgent) that opens newly connected drives in Folix.
// It reacts only to real external storage (USB / Thunderbolt / SATA / SD), not to disk images,
// network shares, Time Machine, helper partitions, virtual CDs or drives the user chose to ignore.
// Installed as ~/Library/LaunchAgents/com.umid.folix.agent.plist; FOLIX_AGENT_DEBUG=1 prints each decision.

#include <CoreFoundation/CoreFoundation.h>
#include <DiskArbitration/DiskArbitration.h>

#include <mach-o/dyld.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <set>
#include <string>

extern char** environ;

namespace {

time_t g_start = 0;
bool g_debug = false;   // FOLIX_AGENT_DEBUG=1 prints every decision
std::map<std::string, time_t> g_recent;

std::string supportDir()
{
    const char* home = getenv("HOME");
    return std::string(home ? home : "") + "/Library/Application Support/Folix";
}

std::string appBundle()
{
    char buf[PATH_MAX];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) != 0)
        return "/Applications/Folix.app";
    std::string p = buf;   // .../Folix.app/Contents/MacOS/FolixAgent
    for (int i = 0; i < 3; ++i)
        p = p.substr(0, p.rfind('/'));
    return p;
}

std::string str(CFTypeRef v)
{
    if (!v || CFGetTypeID(v) != CFStringGetTypeID())
        return {};
    char buf[512];
    if (!CFStringGetCString(static_cast<CFStringRef>(v), buf, sizeof(buf), kCFStringEncodingUTF8))
        return {};
    return buf;
}

bool flag(CFDictionaryRef d, CFStringRef key, bool fallback)
{
    const void* v = CFDictionaryGetValue(d, key);
    if (!v || CFGetTypeID(v) != CFBooleanGetTypeID())
        return fallback;
    return CFBooleanGetValue(static_cast<CFBooleanRef>(v));
}

long long number(CFDictionaryRef d, CFStringRef key)
{
    const void* v = CFDictionaryGetValue(d, key);
    long long n = -1;
    if (v && CFGetTypeID(v) == CFNumberGetTypeID())
        CFNumberGetValue(static_cast<CFNumberRef>(v), kCFNumberLongLongType, &n);
    return n;
}

bool ignored(const std::string& uuid)
{
    if (uuid.empty())
        return false;
    std::ifstream in(supportDir() + "/ignored-volumes.txt");
    std::string line;
    while (std::getline(in, line)) {
        if (line == uuid)
            return true;
    }
    return false;
}

bool exists(const std::string& path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

void run(const char* const* argv)
{
    pid_t pid;
    if (posix_spawn(&pid, argv[0], nullptr, nullptr, const_cast<char* const*>(argv), environ) == 0) {
        int status = 0;
        waitpid(pid, &status, 0);
    }
}

void openInFolix(const std::string& path)
{
    const std::string app = appBundle();
    // A running Folix listens on a local socket; otherwise start it with the path.
    const std::string sock = supportDir() + "/folix.sock";
    const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, sock.c_str(), sizeof(addr.sun_path) - 1);
    if (fd >= 0 && connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
        const std::string msg = "open\t" + path + "\n";
        write(fd, msg.data(), msg.size());
        close(fd);
        const char* argv[] = {"/usr/bin/open", "-a", app.c_str(), nullptr};
        run(argv);   // bring the window to the front
        return;
    }
    if (fd >= 0)
        close(fd);
    const char* argv[] = {"/usr/bin/open", "-a", app.c_str(), "--args", "--open", path.c_str(), nullptr};
    run(argv);
}

void consider(DADiskRef disk)
{
    CFDictionaryRef d = DADiskCopyDescription(disk);
    if (!d)
        return;
    const auto url = static_cast<CFURLRef>(CFDictionaryGetValue(d, kDADiskDescriptionVolumePathKey));
    if (!url) {
        CFRelease(d);
        return;
    }
    char buf[PATH_MAX] = {0};
    CFURLGetFileSystemRepresentation(url, true, reinterpret_cast<UInt8*>(buf), sizeof(buf));
    std::string path = buf;
    while (path.size() > 1 && path.back() == '/')
        path.pop_back();

    const bool internal = flag(d, kDADiskDescriptionDeviceInternalKey, true);
    const bool removable = flag(d, kDADiskDescriptionMediaRemovableKey, false);
    const bool network = flag(d, kDADiskDescriptionVolumeNetworkKey, false);
    const std::string protocol = str(CFDictionaryGetValue(d, kDADiskDescriptionDeviceProtocolKey));
    const std::string model = str(CFDictionaryGetValue(d, kDADiskDescriptionDeviceModelKey));
    const std::string kind = str(CFDictionaryGetValue(d, kDADiskDescriptionMediaKindKey));
    const std::string name = str(CFDictionaryGetValue(d, kDADiskDescriptionVolumeNameKey));
    const long long mediaSize = number(d, kDADiskDescriptionMediaSizeKey);
    std::string uuid;
    if (const void* u = CFDictionaryGetValue(d, kDADiskDescriptionVolumeUUIDKey);
        u && CFGetTypeID(u) == CFUUIDGetTypeID()) {
        CFStringRef s = CFUUIDCreateString(nullptr, static_cast<CFUUIDRef>(u));
        uuid = str(s);
        CFRelease(s);
    }
    CFRelease(d);

    static const std::set<std::string> storageProtocols = {
        "USB", "Thunderbolt", "SATA", "PCI-Express", "PCI", "Secure Digital", "FireWire", "SAS", "NVMe",
    };
    static const std::set<std::string> helperNames = {"EFI", "Recovery", "Preboot", "VM", "Update", "System Reserved"};

    bool ok = path.rfind("/Volumes/", 0) == 0 && !network;
    ok = ok && storageProtocols.count(protocol) > 0;
    ok = ok && (!internal || (removable && protocol == "Secure Digital"));   // built-in SD slot
    ok = ok && model != "Disk Image";
    ok = ok && kind != "IOCDMedia" && kind != "IODVDMedia" && kind != "IOBDMedia";   // modem / installer CDs
    ok = ok && (mediaSize < 0 || mediaSize >= 64LL * 1024 * 1024);                   // tiny helper partitions
    ok = ok && !helperNames.count(name) && name.find("Time Machine") == std::string::npos;
    ok = ok && !exists(path + "/Backups.backupdb") && !exists(path + "/.timemachine");
    ok = ok && !ignored(uuid);
    if (g_debug)
        std::fprintf(stderr, "%s: protocol=%s model=%s internal=%d removable=%d kind=%s -> %s\n", path.c_str(),
                     protocol.c_str(), model.c_str(), internal, removable, kind.c_str(), ok ? "OPEN" : "ignore");
    if (!ok)
        return;

    const time_t now = time(nullptr);
    if (now - g_start < 5)
        return;   // drives that were already there when the agent started
    if (auto it = g_recent.find(path); it != g_recent.end() && now - it->second < 15)
        return;
    g_recent[path] = now;
    openInFolix(path);
}

void described(DADiskRef disk, CFArrayRef, void*) { consider(disk); }
void appeared(DADiskRef disk, void*) { consider(disk); }

} // namespace

int main()
{
    g_start = time(nullptr);
    g_debug = getenv("FOLIX_AGENT_DEBUG") != nullptr;
    DASessionRef session = DASessionCreate(kCFAllocatorDefault);
    if (!session)
        return 1;
    CFMutableArrayRef keys = CFArrayCreateMutable(nullptr, 0, &kCFTypeArrayCallBacks);
    CFArrayAppendValue(keys, kDADiskDescriptionVolumePathKey);
    DARegisterDiskDescriptionChangedCallback(session, nullptr, keys, described, nullptr);
    DARegisterDiskAppearedCallback(session, kDADiskDescriptionMatchVolumeMountable, appeared, nullptr);
    DASessionScheduleWithRunLoop(session, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    CFRunLoopRun();
    return 0;
}
