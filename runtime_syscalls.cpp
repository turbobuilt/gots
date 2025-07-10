#include "runtime_syscalls.h"
#include "runtime.h"
#include "runtime_object.h"
#include "lock_system.h"

// Forward declarations for new goroutine system
extern "C" {
    int64_t __gots_set_timeout(void* callback, int64_t delay_ms);
    int64_t __gots_set_interval(void* callback, int64_t delay_ms);  
    bool __gots_clear_timeout(int64_t timer_id);
    bool __gots_clear_interval(int64_t timer_id);
}

// Forward declarations for simplified timer system
extern std::atomic<int64_t> g_timer_id_counter;
extern std::atomic<int64_t> g_active_timer_count;
extern std::atomic<int64_t> g_active_goroutine_count;

// Simple timer functions
int64_t create_timer(int64_t delay_ms, void* callback, bool is_interval = false);
bool has_active_timers();
bool has_active_work();

// Forward declarations for new timer system
namespace gots {
    class GoroutineTimerManager;
    GoroutineTimerManager& get_timer_manager();
    extern thread_local std::unique_ptr<GoroutineTimerManager> g_thread_timer_manager;
}

// New timer system functions
int64_t create_timer_new(int64_t delay_ms, void* callback, bool is_interval = false);
bool cancel_timer_new(int64_t timer_id);
bool has_active_timers_new();
bool has_active_work_new();
#include <chrono>
#include <thread>
#include <ctime>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <cstring>
#include <random>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/utsname.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <signal.h>
#include <termios.h>
#include <sys/ioctl.h>

namespace gots {

// Global runtime object instance
RuntimeObject* global_runtime = nullptr;

// Method registry for JIT optimization
std::unordered_map<std::string, RuntimeMethodInfo> runtime_method_registry;

// Time and Date syscalls
extern "C" {

int64_t __runtime_time_now_millis() {
    std::cout << "DEBUG: __runtime_time_now_millis called!" << std::endl;
    
    // Use the same approach as __date_now for consistency
    auto now = std::chrono::system_clock::now();
    auto time_since_epoch = now.time_since_epoch();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(time_since_epoch).count();
    
    std::cout << "DEBUG: Returning timestamp: " << millis << std::endl;
    return millis;
}

int64_t __runtime_time_now_nanos() {
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
}

int64_t __runtime_time_timezone_offset() {
    std::time_t now = std::time(nullptr);
    std::tm* local_tm = std::localtime(&now);
    std::tm* utc_tm = std::gmtime(&now);
    
    // Calculate offset in minutes (JavaScript style)
    int64_t local_minutes = local_tm->tm_hour * 60 + local_tm->tm_min;
    int64_t utc_minutes = utc_tm->tm_hour * 60 + utc_tm->tm_min;
    
    // Handle day boundary
    if (local_tm->tm_mday != utc_tm->tm_mday) {
        if (local_tm->tm_mday > utc_tm->tm_mday) {
            local_minutes += 24 * 60;
        } else {
            utc_minutes += 24 * 60;
        }
    }
    
    return utc_minutes - local_minutes; // JavaScript returns UTC - local
}

int64_t __runtime_time_daylight_saving() {
    std::time_t now = std::time(nullptr);
    std::tm* local_tm = std::localtime(&now);
    return local_tm->tm_isdst > 0 ? 1 : 0;
}

void __runtime_time_sleep_millis(int64_t millis) {
    std::this_thread::sleep_for(std::chrono::milliseconds(millis));
}

void __runtime_time_sleep_nanos(int64_t nanos) {
    std::this_thread::sleep_for(std::chrono::nanoseconds(nanos));
}

// Process syscalls
int64_t __runtime_process_pid() {
    return static_cast<int64_t>(getpid());
}

int64_t __runtime_process_ppid() {
    return static_cast<int64_t>(getppid());
}

int64_t __runtime_process_uid() {
    return static_cast<int64_t>(getuid());
}

int64_t __runtime_process_gid() {
    return static_cast<int64_t>(getgid());
}

void* __runtime_process_cwd() {
    char buffer[4096];
    if (getcwd(buffer, sizeof(buffer))) {
        return __string_create(buffer);
    }
    return __string_create("");
}

bool __runtime_process_chdir(const char* path) {
    return chdir(path) == 0;
}

void __runtime_process_exit(int64_t code) {
    exit(static_cast<int>(code));
}

void* __runtime_process_argv() {
    // TODO: Implement argv array
    return __array_create(0);
}

void* __runtime_process_env_get(const char* key) {
    const char* value = std::getenv(key);
    if (value) {
        return __string_create(value);
    }
    return nullptr;
}

bool __runtime_process_env_set(const char* key, const char* value) {
    return setenv(key, value, 1) == 0;
}

bool __runtime_process_env_delete(const char* key) {
    return unsetenv(key) == 0;
}

void* __runtime_process_env_keys() {
    // Create array of environment variable names
    void* array = __array_create(0);
    extern char** environ;
    
    for (char** env = environ; *env != nullptr; ++env) {
        std::string entry(*env);
        size_t eq_pos = entry.find('=');
        if (eq_pos != std::string::npos) {
            std::string key = entry.substr(0, eq_pos);
            void* key_str = __string_create(key.c_str());
            __array_push(array, reinterpret_cast<int64_t>(key_str));
        }
    }
    
    return array;
}

int64_t __runtime_process_memory_usage() {
    // Read from /proc/self/status on Linux
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.find("VmRSS:") == 0) {
            int64_t rss_kb;
            sscanf(line.c_str(), "VmRSS: %ld kB", &rss_kb);
            return rss_kb * 1024; // Return in bytes
        }
    }
    return 0;
}

double __runtime_process_cpu_usage() {
    // TODO: Implement CPU usage calculation
    return 0.0;
}

void* __runtime_process_platform() {
    #ifdef __linux__
        return __string_create("linux");
    #elif __APPLE__
        return __string_create("darwin");
    #elif _WIN32
        return __string_create("win32");
    #else
        return __string_create("unknown");
    #endif
}

void* __runtime_process_arch() {
    #if defined(__x86_64__) || defined(_M_X64)
        return __string_create("x64");
    #elif defined(__i386__) || defined(_M_IX86)
        return __string_create("ia32");
    #elif defined(__aarch64__) || defined(_M_ARM64)
        return __string_create("arm64");
    #elif defined(__arm__) || defined(_M_ARM)
        return __string_create("arm");
    #else
        return __string_create("unknown");
    #endif
}

void* __runtime_process_version() {
    // Return GoTS version
    return __string_create("v1.0.0");
}

// File system syscalls
int64_t __runtime_fs_open(const char* path, const char* flags, int64_t mode) {
    int open_flags = 0;
    
    // Parse flags string (Node.js style)
    if (strcmp(flags, "r") == 0) {
        open_flags = O_RDONLY;
    } else if (strcmp(flags, "r+") == 0) {
        open_flags = O_RDWR;
    } else if (strcmp(flags, "w") == 0) {
        open_flags = O_WRONLY | O_CREAT | O_TRUNC;
    } else if (strcmp(flags, "w+") == 0) {
        open_flags = O_RDWR | O_CREAT | O_TRUNC;
    } else if (strcmp(flags, "a") == 0) {
        open_flags = O_WRONLY | O_CREAT | O_APPEND;
    } else if (strcmp(flags, "a+") == 0) {
        open_flags = O_RDWR | O_CREAT | O_APPEND;
    }
    
    return open(path, open_flags, static_cast<mode_t>(mode));
}

int64_t __runtime_fs_close(int64_t fd) {
    return close(static_cast<int>(fd));
}

int64_t __runtime_fs_read(int64_t fd, void* buffer, int64_t size) {
    return read(static_cast<int>(fd), buffer, static_cast<size_t>(size));
}

int64_t __runtime_fs_write(int64_t fd, const void* buffer, int64_t size) {
    return write(static_cast<int>(fd), buffer, static_cast<size_t>(size));
}

int64_t __runtime_fs_seek(int64_t fd, int64_t offset, int64_t whence) {
    return lseek(static_cast<int>(fd), static_cast<off_t>(offset), static_cast<int>(whence));
}

bool __runtime_fs_exists(const char* path) {
    return access(path, F_OK) == 0;
}

bool __runtime_fs_is_file(const char* path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISREG(st.st_mode);
    }
    return false;
}

bool __runtime_fs_is_directory(const char* path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    return false;
}

bool __runtime_fs_is_symlink(const char* path) {
    struct stat st;
    if (lstat(path, &st) == 0) {
        return S_ISLNK(st.st_mode);
    }
    return false;
}

int64_t __runtime_fs_size(const char* path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return static_cast<int64_t>(st.st_size);
    }
    return -1;
}

int64_t __runtime_fs_mtime(const char* path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return static_cast<int64_t>(st.st_mtime) * 1000; // Convert to milliseconds
    }
    return -1;
}

int64_t __runtime_fs_atime(const char* path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return static_cast<int64_t>(st.st_atime) * 1000; // Convert to milliseconds
    }
    return -1;
}

int64_t __runtime_fs_ctime(const char* path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return static_cast<int64_t>(st.st_ctime) * 1000; // Convert to milliseconds
    }
    return -1;
}

bool __runtime_fs_chmod(const char* path, int64_t mode) {
    return chmod(path, static_cast<mode_t>(mode)) == 0;
}

bool __runtime_fs_chown(const char* path, int64_t uid, int64_t gid) {
    return chown(path, static_cast<uid_t>(uid), static_cast<gid_t>(gid)) == 0;
}

bool __runtime_fs_mkdir(const char* path, int64_t mode) {
    return mkdir(path, static_cast<mode_t>(mode)) == 0;
}

bool __runtime_fs_rmdir(const char* path) {
    return rmdir(path) == 0;
}

bool __runtime_fs_unlink(const char* path) {
    return unlink(path) == 0;
}

bool __runtime_fs_rename(const char* from, const char* to) {
    return rename(from, to) == 0;
}

bool __runtime_fs_symlink(const char* target, const char* path) {
    return symlink(target, path) == 0;
}

void* __runtime_fs_readlink(const char* path) {
    char buffer[4096];
    ssize_t len = readlink(path, buffer, sizeof(buffer) - 1);
    if (len != -1) {
        buffer[len] = '\0';
        return __string_create(buffer);
    }
    return nullptr;
}

void* __runtime_fs_realpath(const char* path) {
    char* resolved = realpath(path, nullptr);
    if (resolved) {
        void* result = __string_create(resolved);
        free(resolved);
        return result;
    }
    return nullptr;
}

void* __runtime_fs_readdir(const char* path) {
    void* array = __array_create(0);
    DIR* dir = opendir(path);
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            // Skip . and ..
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                void* name = __string_create(entry->d_name);
                __array_push(array, reinterpret_cast<int64_t>(name));
            }
        }
        closedir(dir);
    }
    return array;
}

bool __runtime_fs_copy(const char* from, const char* to) {
    try {
        std::filesystem::copy(from, to, std::filesystem::copy_options::overwrite_existing);
        return true;
    } catch (...) {
        return false;
    }
}

// OS info syscalls
void* __runtime_os_hostname() {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        return __string_create(hostname);
    }
    return __string_create("localhost");
}

void* __runtime_os_type() {
    struct utsname info;
    if (uname(&info) == 0) {
        return __string_create(info.sysname);
    }
    return __string_create("Unknown");
}

void* __runtime_os_release() {
    struct utsname info;
    if (uname(&info) == 0) {
        return __string_create(info.release);
    }
    return __string_create("");
}

void* __runtime_os_tmpdir() {
    const char* tmpdir = std::getenv("TMPDIR");
    if (!tmpdir) tmpdir = std::getenv("TMP");
    if (!tmpdir) tmpdir = std::getenv("TEMP");
    if (!tmpdir) tmpdir = "/tmp";
    return __string_create(tmpdir);
}

void* __runtime_os_homedir() {
    const char* home = std::getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        if (pw) home = pw->pw_dir;
    }
    return __string_create(home ? home : "");
}

int64_t __runtime_os_uptime() {
    // Read from /proc/uptime on Linux
    std::ifstream uptime_file("/proc/uptime");
    double uptime_seconds;
    if (uptime_file >> uptime_seconds) {
        return static_cast<int64_t>(uptime_seconds);
    }
    return 0;
}

int64_t __runtime_os_freemem() {
    // Read from /proc/meminfo on Linux
    std::ifstream meminfo("/proc/meminfo");
    std::string line;
    while (std::getline(meminfo, line)) {
        if (line.find("MemAvailable:") == 0) {
            int64_t mem_kb;
            sscanf(line.c_str(), "MemAvailable: %ld kB", &mem_kb);
            return mem_kb * 1024; // Return in bytes
        }
    }
    return 0;
}

int64_t __runtime_os_totalmem() {
    // Read from /proc/meminfo on Linux
    std::ifstream meminfo("/proc/meminfo");
    std::string line;
    while (std::getline(meminfo, line)) {
        if (line.find("MemTotal:") == 0) {
            int64_t mem_kb;
            sscanf(line.c_str(), "MemTotal: %ld kB", &mem_kb);
            return mem_kb * 1024; // Return in bytes
        }
    }
    return 0;
}

// Math extensions
double __runtime_math_random() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_real_distribution<> dis(0.0, 1.0);
    return dis(gen);
}

void __runtime_math_random_seed(int64_t seed) {
    static std::mt19937 gen(static_cast<unsigned int>(seed));
}

// Network syscalls - comprehensive socket and networking operations
int64_t __runtime_net_socket(int64_t domain, int64_t type, int64_t protocol) {
    return socket(static_cast<int>(domain), static_cast<int>(type), static_cast<int>(protocol));
}

bool __runtime_net_bind(int64_t sockfd, const char* address, int64_t port) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    
    if (strcmp(address, "0.0.0.0") == 0 || strcmp(address, "localhost") == 0) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (inet_pton(AF_INET, address, &addr.sin_addr) <= 0) {
            return false;
        }
    }
    
    return bind(static_cast<int>(sockfd), reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0;
}

bool __runtime_net_listen(int64_t sockfd, int64_t backlog) {
    return listen(static_cast<int>(sockfd), static_cast<int>(backlog)) == 0;
}

int64_t __runtime_net_accept(int64_t sockfd, void* address) {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    int client_fd = accept(static_cast<int>(sockfd), reinterpret_cast<struct sockaddr*>(&client_addr), &addr_len);
    
    // Store client address info if requested
    if (address && client_fd >= 0) {
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, INET_ADDRSTRLEN);
        
        // Create an object with address info (simplified - would need proper object creation)
        // For now, just convert to string format
        void* addr_str = __string_create(ip_str);
        *reinterpret_cast<void**>(address) = addr_str;
    }
    
    return static_cast<int64_t>(client_fd);
}

bool __runtime_net_connect(int64_t sockfd, const char* address, int64_t port) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    
    if (inet_pton(AF_INET, address, &addr.sin_addr) <= 0) {
        return false;
    }
    
    return connect(static_cast<int>(sockfd), reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0;
}

int64_t __runtime_net_send(int64_t sockfd, const void* buffer, int64_t size, int64_t flags) {
    return send(static_cast<int>(sockfd), buffer, static_cast<size_t>(size), static_cast<int>(flags));
}

int64_t __runtime_net_recv(int64_t sockfd, void* buffer, int64_t size, int64_t flags) {
    return recv(static_cast<int>(sockfd), buffer, static_cast<size_t>(size), static_cast<int>(flags));
}

bool __runtime_net_close(int64_t sockfd) {
    return close(static_cast<int>(sockfd)) == 0;
}

bool __runtime_net_shutdown(int64_t sockfd, int64_t how) {
    return shutdown(static_cast<int>(sockfd), static_cast<int>(how)) == 0;
}

bool __runtime_net_setsockopt(int64_t sockfd, int64_t level, int64_t optname, const void* optval, int64_t optlen) {
    return setsockopt(static_cast<int>(sockfd), static_cast<int>(level), static_cast<int>(optname), 
                     optval, static_cast<socklen_t>(optlen)) == 0;
}

bool __runtime_net_getsockopt(int64_t sockfd, int64_t level, int64_t optname, void* optval, int64_t* optlen) {
    socklen_t len = static_cast<socklen_t>(*optlen);
    bool result = getsockopt(static_cast<int>(sockfd), static_cast<int>(level), static_cast<int>(optname), 
                            optval, &len) == 0;
    *optlen = static_cast<int64_t>(len);
    return result;
}

void* __runtime_net_gethostbyname(const char* hostname) {
    struct hostent* host = gethostbyname(hostname);
    if (host && host->h_addr_list[0]) {
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, host->h_addr_list[0], ip_str, INET_ADDRSTRLEN);
        return __string_create(ip_str);
    }
    return nullptr;
}

void* __runtime_net_gethostbyaddr(const char* addr, int64_t len, int64_t type) {
    struct hostent* host = gethostbyaddr(addr, static_cast<socklen_t>(len), static_cast<int>(type));
    if (host && host->h_name) {
        return __string_create(host->h_name);
    }
    return nullptr;
}

// DNS syscalls - high-performance domain resolution
void* __runtime_dns_lookup(const char* hostname, int64_t family) {
    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = static_cast<int>(family); // AF_INET or AF_INET6
    
    int status = getaddrinfo(hostname, nullptr, &hints, &result);
    if (status == 0 && result) {
        void* array = __array_create(0);
        
        for (struct addrinfo* p = result; p != nullptr; p = p->ai_next) {
            char ip_str[INET6_ADDRSTRLEN];
            void* addr_ptr;
            
            if (p->ai_family == AF_INET) {
                struct sockaddr_in* ipv4 = reinterpret_cast<struct sockaddr_in*>(p->ai_addr);
                addr_ptr = &(ipv4->sin_addr);
                inet_ntop(AF_INET, addr_ptr, ip_str, INET_ADDRSTRLEN);
            } else if (p->ai_family == AF_INET6) {
                struct sockaddr_in6* ipv6 = reinterpret_cast<struct sockaddr_in6*>(p->ai_addr);
                addr_ptr = &(ipv6->sin6_addr);
                inet_ntop(AF_INET6, addr_ptr, ip_str, INET6_ADDRSTRLEN);
            } else {
                continue;
            }
            
            void* ip_string = __string_create(ip_str);
            __array_push(array, reinterpret_cast<int64_t>(ip_string));
        }
        
        freeaddrinfo(result);
        return array;
    }
    
    return __array_create(0); // Return empty array on failure
}

void* __runtime_dns_reverse(const char* ip) {
    struct sockaddr_in sa;
    char hostname[NI_MAXHOST];
    
    sa.sin_family = AF_INET;
    if (inet_pton(AF_INET, ip, &sa.sin_addr) == 1) {
        if (getnameinfo(reinterpret_cast<struct sockaddr*>(&sa), sizeof(sa), 
                       hostname, NI_MAXHOST, nullptr, 0, NI_NAMEREQD) == 0) {
            return __string_create(hostname);
        }
    }
    
    return nullptr;
}

void* __runtime_dns_resolve4(const char* hostname) {
    return __runtime_dns_lookup(hostname, AF_INET);
}

void* __runtime_dns_resolve6(const char* hostname) {
    return __runtime_dns_lookup(hostname, AF_INET6);
}

void* __runtime_dns_resolveMx(const char* hostname) {
    // MX record resolution would require more complex DNS library
    // For now, return empty array - could integrate with c-ares later
    return __array_create(0);
}

void* __runtime_dns_resolveTxt(const char* hostname) {
    // TXT record resolution - placeholder for now
    return __array_create(0);
}

void* __runtime_dns_resolveSrv(const char* hostname) {
    // SRV record resolution - placeholder for now
    return __array_create(0);
}

void* __runtime_dns_resolveNs(const char* hostname) {
    // NS record resolution - placeholder for now
    return __array_create(0);
}

void* __runtime_dns_resolveCname(const char* hostname) {
    // CNAME record resolution - placeholder for now
    return __array_create(0);
}

// Basic HTTP syscalls - simplified HTTP client/server functionality
void* __runtime_http_request(const char* method, const char* url, void* headers, const void* body, int64_t body_size) {
    // This is a simplified HTTP client implementation
    // In a full implementation, this would parse the URL, create socket connections,
    // format HTTP requests, and return promise objects
    
    // For now, return a placeholder response object
    // Real implementation would use libcurl or custom HTTP parser
    return __string_create("HTTP response placeholder");
}

void* __runtime_http_create_server(void* handler) {
    // HTTP server creation - would return server object
    // Real implementation would create event loop integration
    return nullptr; // Placeholder
}

bool __runtime_http_server_listen(void* server, int64_t port, const char* host) {
    // HTTP server listen - would bind to port and start accepting connections
    return false; // Placeholder
}

// Crypto syscalls - secure cryptographic operations
void* __runtime_crypto_random_bytes(int64_t size) {
    if (size <= 0) return nullptr;
    
    void* buffer = malloc(static_cast<size_t>(size));
    if (!buffer) return nullptr;
    
    // Use secure random number generation
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<unsigned char> dis(0, 255);
    
    unsigned char* bytes = static_cast<unsigned char*>(buffer);
    for (int64_t i = 0; i < size; ++i) {
        bytes[i] = dis(gen);
    }
    
    // Would normally return a Buffer object, but for now return raw pointer
    return buffer;
}

void* __runtime_crypto_pbkdf2(const char* password, const char* salt, int64_t iterations, int64_t keylen, const char* digest) {
    // PBKDF2 key derivation - would need OpenSSL integration
    // Placeholder implementation
    return __runtime_crypto_random_bytes(keylen);
}

void* __runtime_crypto_create_hash(const char* algorithm) {
    // Hash object creation - would return hash context
    // Real implementation needs OpenSSL or similar crypto library
    return nullptr; // Placeholder
}

void* __runtime_crypto_create_hmac(const char* algorithm, const char* key) {
    // HMAC object creation - would return HMAC context  
    return nullptr; // Placeholder
}

void* __runtime_crypto_hash_update(void* hash, const void* data, int64_t size) {
    // Hash update operation
    return hash; // Placeholder
}

void* __runtime_crypto_hash_digest(void* hash, const char* encoding) {
    // Hash finalization and encoding
    return __string_create("hash_placeholder"); // Placeholder
}

void* __runtime_crypto_sign(const char* algorithm, const void* data, int64_t size, const char* key) {
    // Digital signing - needs crypto library integration
    return __string_create("signature_placeholder"); // Placeholder
}

void* __runtime_crypto_verify(const char* algorithm, const void* data, int64_t size, const char* key, const void* signature, int64_t sig_size) {
    // Signature verification
    return nullptr; // Placeholder - would return boolean
}

// Buffer/Binary syscalls - high-performance binary data handling
void* __runtime_buffer_alloc(int64_t size) {
    if (size <= 0) return nullptr;
    
    // Allocate buffer with size header for GoTS Buffer object
    void* buffer = malloc(static_cast<size_t>(size + sizeof(int64_t)));
    if (!buffer) return nullptr;
    
    // Store size at beginning of buffer
    *reinterpret_cast<int64_t*>(buffer) = size;
    
    // Zero-initialize the buffer data
    memset(static_cast<char*>(buffer) + sizeof(int64_t), 0, static_cast<size_t>(size));
    
    return buffer;
}

void* __runtime_buffer_from_string(const char* str, const char* encoding) {
    if (!str) return nullptr;
    
    size_t str_len = strlen(str);
    
    if (strcmp(encoding, "utf8") == 0 || strcmp(encoding, "utf-8") == 0) {
        // UTF-8 encoding - direct copy
        void* buffer = __runtime_buffer_alloc(static_cast<int64_t>(str_len));
        if (buffer) {
            char* data = static_cast<char*>(buffer) + sizeof(int64_t);
            memcpy(data, str, str_len);
        }
        return buffer;
    } else if (strcmp(encoding, "base64") == 0) {
        // Base64 decoding - simplified implementation
        size_t decoded_size = (str_len * 3) / 4; // Approximate size
        void* buffer = __runtime_buffer_alloc(static_cast<int64_t>(decoded_size));
        // Real implementation would do proper base64 decoding
        return buffer;
    } else if (strcmp(encoding, "hex") == 0) {
        // Hex decoding
        size_t decoded_size = str_len / 2;
        void* buffer = __runtime_buffer_alloc(static_cast<int64_t>(decoded_size));
        if (buffer) {
            char* data = static_cast<char*>(buffer) + sizeof(int64_t);
            for (size_t i = 0; i < decoded_size; ++i) {
                sscanf(str + 2*i, "%2hhx", reinterpret_cast<unsigned char*>(data + i));
            }
        }
        return buffer;
    }
    
    return nullptr;
}

void* __runtime_buffer_from_array(void* array) {
    // Convert GoTS array to buffer
    if (!array) return nullptr;
    
    // This would need integration with GoTS array implementation
    // For now, return a placeholder buffer
    return __runtime_buffer_alloc(0);
}

void* __runtime_buffer_concat(void* list) {
    // Concatenate array of buffers
    if (!list) return nullptr;
    
    // This would iterate through buffer array and concatenate them
    // Placeholder implementation
    return __runtime_buffer_alloc(0);
}

int64_t __runtime_buffer_length(void* buffer) {
    if (!buffer) return 0;
    return *reinterpret_cast<int64_t*>(buffer);
}

void* __runtime_buffer_slice(void* buffer, int64_t start, int64_t end) {
    if (!buffer) return nullptr;
    
    int64_t size = __runtime_buffer_length(buffer);
    if (start < 0) start = 0;
    if (end > size) end = size;
    if (start >= end) return __runtime_buffer_alloc(0);
    
    int64_t slice_size = end - start;
    void* new_buffer = __runtime_buffer_alloc(slice_size);
    if (new_buffer) {
        char* src_data = static_cast<char*>(buffer) + sizeof(int64_t) + start;
        char* dst_data = static_cast<char*>(new_buffer) + sizeof(int64_t);
        memcpy(dst_data, src_data, static_cast<size_t>(slice_size));
    }
    
    return new_buffer;
}

bool __runtime_buffer_equals(void* buf1, void* buf2) {
    if (!buf1 || !buf2) return false;
    
    int64_t size1 = __runtime_buffer_length(buf1);
    int64_t size2 = __runtime_buffer_length(buf2);
    
    if (size1 != size2) return false;
    
    char* data1 = static_cast<char*>(buf1) + sizeof(int64_t);
    char* data2 = static_cast<char*>(buf2) + sizeof(int64_t);
    
    return memcmp(data1, data2, static_cast<size_t>(size1)) == 0;
}

int64_t __runtime_buffer_compare(void* buf1, void* buf2) {
    if (!buf1 || !buf2) return 0;
    
    int64_t size1 = __runtime_buffer_length(buf1);
    int64_t size2 = __runtime_buffer_length(buf2);
    int64_t min_size = (size1 < size2) ? size1 : size2;
    
    char* data1 = static_cast<char*>(buf1) + sizeof(int64_t);
    char* data2 = static_cast<char*>(buf2) + sizeof(int64_t);
    
    int result = memcmp(data1, data2, static_cast<size_t>(min_size));
    if (result != 0) return result;
    
    // If data is equal, compare lengths
    return (size1 < size2) ? -1 : (size1 > size2) ? 1 : 0;
}

void* __runtime_buffer_to_string(void* buffer, const char* encoding) {
    if (!buffer) return nullptr;
    
    int64_t size = __runtime_buffer_length(buffer);
    char* data = static_cast<char*>(buffer) + sizeof(int64_t);
    
    if (strcmp(encoding, "utf8") == 0 || strcmp(encoding, "utf-8") == 0) {
        // Create null-terminated string
        char* str = static_cast<char*>(malloc(static_cast<size_t>(size + 1)));
        if (str) {
            memcpy(str, data, static_cast<size_t>(size));
            str[size] = '\0';
            void* result = __string_create(str);
            free(str);
            return result;
        }
    } else if (strcmp(encoding, "hex") == 0) {
        // Hex encoding
        char* hex_str = static_cast<char*>(malloc(static_cast<size_t>(size * 2 + 1)));
        if (hex_str) {
            for (int64_t i = 0; i < size; ++i) {
                sprintf(hex_str + 2*i, "%02x", static_cast<unsigned char>(data[i]));
            }
            hex_str[size * 2] = '\0';
            void* result = __string_create(hex_str);
            free(hex_str);
            return result;
        }
    }
    
    return nullptr;
}

// Path syscalls - cross-platform path manipulation
void* __runtime_path_basename(const char* path, const char* ext) {
    if (!path) return __string_create("");
    
    // Find last path separator
    const char* last_sep = nullptr;
    for (const char* p = path; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            last_sep = p;
        }
    }
    
    const char* basename = last_sep ? last_sep + 1 : path;
    
    // Remove extension if provided
    if (ext && strlen(ext) > 0) {
        size_t basename_len = strlen(basename);
        size_t ext_len = strlen(ext);
        
        if (basename_len > ext_len && 
            strcmp(basename + basename_len - ext_len, ext) == 0) {
            char* result = static_cast<char*>(malloc(basename_len - ext_len + 1));
            if (result) {
                strncpy(result, basename, basename_len - ext_len);
                result[basename_len - ext_len] = '\0';
                void* str = __string_create(result);
                free(result);
                return str;
            }
        }
    }
    
    return __string_create(basename);
}

void* __runtime_path_dirname(const char* path) {
    if (!path) return __string_create(".");
    
    // Find last path separator
    const char* last_sep = nullptr;
    for (const char* p = path; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            last_sep = p;
        }
    }
    
    if (!last_sep) return __string_create(".");
    
    // Handle root directory
    if (last_sep == path) return __string_create("/");
    
    size_t dirname_len = last_sep - path;
    char* dirname = static_cast<char*>(malloc(dirname_len + 1));
    if (dirname) {
        strncpy(dirname, path, dirname_len);
        dirname[dirname_len] = '\0';
        void* result = __string_create(dirname);
        free(dirname);
        return result;
    }
    
    return __string_create(".");
}

void* __runtime_path_extname(const char* path) {
    if (!path) return __string_create("");
    
    // Find last dot after last path separator
    const char* last_sep = nullptr;
    const char* last_dot = nullptr;
    
    for (const char* p = path; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            last_sep = p;
            last_dot = nullptr; // Reset dot search after separator
        } else if (*p == '.') {
            last_dot = p;
        }
    }
    
    // Return extension including the dot
    return last_dot ? __string_create(last_dot) : __string_create("");
}

void* __runtime_path_join(void* paths) {
    // Join array of path components
    if (!paths) return __string_create("");
    
    // This would iterate through the path array and join with appropriate separators
    // For now, return a placeholder
    return __string_create("/path/joined");
}

void* __runtime_path_normalize(const char* path) {
    if (!path) return __string_create("");
    
    // Normalize path by resolving . and .. components
    // This is a simplified implementation
    std::string normalized(path);
    
    // Replace backslashes with forward slashes for consistency
    for (char& c : normalized) {
        if (c == '\\') c = '/';
    }
    
    // Remove duplicate slashes
    size_t pos = 0;
    while ((pos = normalized.find("//", pos)) != std::string::npos) {
        normalized.erase(pos, 1);
    }
    
    return __string_create(normalized.c_str());
}

void* __runtime_path_resolve(void* paths) {
    // Resolve array of paths to absolute path
    // For now, return current working directory
    return __runtime_process_cwd();
}

bool __runtime_path_is_absolute(const char* path) {
    if (!path || strlen(path) == 0) return false;
    
    #ifdef _WIN32
        // Windows: check for drive letter or UNC path
        return (strlen(path) >= 2 && path[1] == ':') || 
               (strlen(path) >= 2 && path[0] == '\\' && path[1] == '\\');
    #else
        // Unix: check for leading slash
        return path[0] == '/';
    #endif
}

char __runtime_path_sep() {
    #ifdef _WIN32
        return '\\';
    #else
        return '/';
    #endif
}

char __runtime_path_delimiter() {
    #ifdef _WIN32
        return ';';
    #else
        return ':';
    #endif
}

// Child process syscalls - process spawning and management
void* __runtime_child_spawn(const char* command, void* args, void* options) {
    // Spawn child process with arguments
    // This would create a child process object with stdin/stdout/stderr streams
    // For now, return placeholder process object
    return nullptr; // Placeholder
}

void* __runtime_child_exec(const char* command, void* options) {
    // Execute command and return result
    // This would run command and capture output
    return __string_create("exec output placeholder");
}

bool __runtime_child_kill(int64_t pid, int64_t signal) {
    return kill(static_cast<pid_t>(pid), static_cast<int>(signal)) == 0;
}

// Timer syscalls - now handled by MainThreadTimerManager

// Forward declaration for function lookup
extern "C" const char* __lookup_function_name_by_id(int64_t function_id);

// Global storage for deferred timer requests
static std::vector<std::pair<int64_t, int64_t>> deferred_timer_requests;
static std::mutex deferred_mutex;

// Old event loop code removed - timer management is now handled by MainThreadTimerManager

// Timer management is now handled by MainThreadTimerManager

int64_t __runtime_timer_set_timeout(void* callback, int64_t delay) {
    std::cout << "DEBUG: __runtime_timer_set_timeout called with callback=" << callback << ", delay=" << delay << std::endl;
    
    // Use new goroutine timer system
    return __gots_set_timeout(callback, delay);
}

// Function to wait for all active timers to complete
void __runtime_timer_wait_all() {
    std::cout << "DEBUG: __runtime_timer_wait_all called (new timer system)!" << std::endl;
    std::cout.flush();
    
    // In new system, timer processing is handled by goroutine event loops
    // This function is just a placeholder now
}

// Add this function to runtime cleanup to stop the event loop
void __runtime_timer_cleanup() {
    std::cout << "DEBUG: Timer cleanup called (new timer system)" << std::endl;
    
    // In new system, cleanup is handled by goroutine scheduler
    // This function is just a placeholder now
}

// Process deferred timers - placeholder for old system
void __runtime_process_deferred_timers() {
    std::cout << "DEBUG: __runtime_process_deferred_timers called (new timer system)!" << std::endl;
    
    // In new system, timer processing is handled by goroutine event loops
    // This function is just a placeholder now
}

int64_t __runtime_timer_set_interval(void* callback, int64_t delay) {
    std::cout << "DEBUG: __runtime_timer_set_interval called (new timer system)!" << std::endl;
    
    // Use new timer system directly (bypass wrapper)
    int64_t timer_id = __gots_set_interval(callback, delay);
    
    std::cout << "DEBUG: Interval timer " << timer_id << " created with " << delay << "ms delay using new system" << std::endl;
    return timer_id;
}

int64_t __runtime_timer_set_immediate(void* callback) {
    // setImmediate executes callback on next event loop iteration
    return __runtime_timer_set_timeout(callback, 0);
}

// Function removed - now defined earlier in the file
bool __runtime_timer_clear_timeout(int64_t id) {
    std::cout << "DEBUG: clearTimeout called for id=" << id << " (new timer system)" << std::endl;
    
    // Use the new timer cancellation system directly (bypass wrapper)
    bool result = __gots_clear_timeout(id);
    std::cout << "DEBUG: Timer " << id << (result ? " cancelled successfully" : " cancellation failed") << " using new system" << std::endl;
    return result;
}

bool __runtime_timer_clear_interval(int64_t id) {
    return __runtime_timer_clear_timeout(id);
}

bool __runtime_timer_clear_immediate(int64_t id) {
    return __runtime_timer_clear_timeout(id);
}

// URL syscalls - URL parsing and manipulation
struct ParsedURL {
    std::string protocol;
    std::string hostname;
    std::string port;
    std::string pathname;
    std::string search;
    std::string hash;
};

ParsedURL parse_url_internal(const char* url_str) {
    ParsedURL url;
    std::string url_string(url_str);
    
    // Find protocol
    size_t protocol_end = url_string.find("://");
    if (protocol_end != std::string::npos) {
        url.protocol = url_string.substr(0, protocol_end);
        url_string = url_string.substr(protocol_end + 3);
    }
    
    // Find hash
    size_t hash_pos = url_string.find('#');
    if (hash_pos != std::string::npos) {
        url.hash = url_string.substr(hash_pos);
        url_string = url_string.substr(0, hash_pos);
    }
    
    // Find search/query
    size_t search_pos = url_string.find('?');
    if (search_pos != std::string::npos) {
        url.search = url_string.substr(search_pos);
        url_string = url_string.substr(0, search_pos);
    }
    
    // Find pathname
    size_t path_pos = url_string.find('/');
    if (path_pos != std::string::npos) {
        url.pathname = url_string.substr(path_pos);
        url_string = url_string.substr(0, path_pos);
    } else {
        url.pathname = "/";
    }
    
    // Parse hostname and port
    size_t port_pos = url_string.find(':');
    if (port_pos != std::string::npos) {
        url.hostname = url_string.substr(0, port_pos);
        url.port = url_string.substr(port_pos + 1);
    } else {
        url.hostname = url_string;
    }
    
    return url;
}

void* __runtime_url_parse(const char* url, bool parse_query) {
    if (!url) return nullptr;
    
    ParsedURL parsed = parse_url_internal(url);
    
    // Create URL object (simplified - would need proper object creation)
    // For now, return a formatted string representation
    std::string result = "{"
        "\"protocol\":\"" + parsed.protocol + "\","
        "\"hostname\":\"" + parsed.hostname + "\","
        "\"port\":\"" + parsed.port + "\","
        "\"pathname\":\"" + parsed.pathname + "\","
        "\"search\":\"" + parsed.search + "\","
        "\"hash\":\"" + parsed.hash + "\""
        "}";
    
    return __string_create(result.c_str());
}

void* __runtime_url_format(void* url_object) {
    // Format URL object back to string
    // Placeholder implementation
    return __string_create("http://example.com/");
}

void* __runtime_url_resolve(const char* from, const char* to) {
    if (!from || !to) return nullptr;
    
    // Simplified URL resolution
    ParsedURL base = parse_url_internal(from);
    
    if (to[0] == '/') {
        // Absolute path
        std::string result = base.protocol + "://" + base.hostname;
        if (!base.port.empty()) {
            result += ":" + base.port;
        }
        result += to;
        return __string_create(result.c_str());
    } else {
        // Relative path - simplified implementation
        return __string_create(to);
    }
}

// Query string syscalls - URL parameter parsing
void* __runtime_querystring_parse(const char* str, const char* sep, const char* eq) {
    if (!str) return __array_create(0);
    
    std::string query(str);
    std::string separator = sep ? sep : "&";
    std::string equals = eq ? eq : "=";
    
    // Create object to store key-value pairs
    // For now, return array of strings
    void* result = __array_create(0);
    
    size_t pos = 0;
    while (pos < query.length()) {
        size_t next_sep = query.find(separator, pos);
        if (next_sep == std::string::npos) next_sep = query.length();
        
        std::string pair = query.substr(pos, next_sep - pos);
        size_t eq_pos = pair.find(equals);
        
        if (eq_pos != std::string::npos) {
            std::string key = pair.substr(0, eq_pos);
            std::string value = pair.substr(eq_pos + equals.length());
            
            // URL decode key and value (simplified)
            void* key_str = __string_create(key.c_str());
            void* value_str = __string_create(value.c_str());
            
            __array_push(result, reinterpret_cast<int64_t>(key_str));
            __array_push(result, reinterpret_cast<int64_t>(value_str));
        }
        
        pos = next_sep + separator.length();
    }
    
    return result;
}

void* __runtime_querystring_stringify(void* obj, const char* sep, const char* eq) {
    // Convert object to query string
    // Placeholder implementation
    return __string_create("key=value&foo=bar");
}

// Util syscalls - type checking and utility functions
void* __runtime_util_format(const char* format, void* args) {
    if (!format) return __string_create("");
    
    // Simplified printf-style formatting
    // Real implementation would handle various format specifiers
    return __string_create(format);
}

void* __runtime_util_inspect(void* object, void* options) {
    // Object inspection for debugging
    // Placeholder implementation
    return __string_create("[object Object]");
}

bool __runtime_util_is_array(void* value) {
    // Check if value is an array
    // Would need integration with GoTS type system
    return value != nullptr; // Placeholder
}

bool __runtime_util_is_date(void* value) {
    // Check if value is a Date object
    return false; // Placeholder
}

bool __runtime_util_is_error(void* value) {
    // Check if value is an Error object
    return false; // Placeholder
}

bool __runtime_util_is_function(void* value) {
    // Check if value is a function
    return false; // Placeholder
}

bool __runtime_util_is_null(void* value) {
    return value == nullptr;
}

bool __runtime_util_is_number(void* value) {
    // Check if value is a number
    return false; // Placeholder
}

bool __runtime_util_is_object(void* value) {
    // Check if value is an object
    return value != nullptr; // Placeholder
}

bool __runtime_util_is_primitive(void* value) {
    // Check if value is a primitive type
    return false; // Placeholder
}

bool __runtime_util_is_regexp(void* value) {
    // Check if value is a RegExp object
    return false; // Placeholder
}

bool __runtime_util_is_string(void* value) {
    // Check if value is a string
    return false; // Placeholder
}

bool __runtime_util_is_symbol(void* value) {
    // Check if value is a symbol
    return false; // Placeholder
}

bool __runtime_util_is_undefined(void* value) {
    // Check if value is undefined
    return value == nullptr; // Simplified
}

// Performance syscalls - performance monitoring and measurement
int64_t __runtime_perf_now() {
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
}

static std::unordered_map<std::string, int64_t> performance_marks;

void __runtime_perf_mark(const char* name) {
    if (name) {
        performance_marks[name] = __runtime_perf_now();
    }
}

void __runtime_perf_measure(const char* name, const char* start_mark, const char* end_mark) {
    // Create performance measurement between marks
    // Placeholder implementation
}

void* __runtime_perf_get_entries() {
    // Return performance entries
    return __array_create(0);
}

void* __runtime_perf_get_entries_by_name(const char* name) {
    // Return performance entries by name
    return __array_create(0);
}

void* __runtime_perf_get_entries_by_type(const char* type) {
    // Return performance entries by type
    return __array_create(0);
}

// Console/TTY syscalls - terminal interaction
bool __runtime_tty_is_tty(int64_t fd) {
    return isatty(static_cast<int>(fd)) == 1;
}

void* __runtime_tty_get_window_size() {
    // Get terminal window size
    // Would use ioctl to get actual size
    void* result = __array_create(2);
    __array_push(result, 80);  // Default width
    __array_push(result, 24);  // Default height
    return result;
}

bool __runtime_tty_set_raw_mode(int64_t fd, bool enable) {
    // Set terminal raw mode
    // Would use termios functions
    return true; // Placeholder
}

void* __runtime_readline_create_interface(void* input, void* output) {
    // Create readline interface
    return nullptr; // Placeholder
}

// Event emitter syscalls - Node.js-style event system
struct EventEmitter {
    std::unordered_map<std::string, std::vector<void*>> listeners;
    std::mutex listeners_mutex;
};

void* __runtime_events_create_emitter() {
    return new EventEmitter();
}

bool __runtime_events_on(void* emitter, const char* event, void* listener) {
    if (!emitter || !event || !listener) return false;
    
    EventEmitter* ee = static_cast<EventEmitter*>(emitter);
    std::lock_guard<std::mutex> lock(ee->listeners_mutex);
    ee->listeners[event].push_back(listener);
    return true;
}

bool __runtime_events_once(void* emitter, const char* event, void* listener) {
    // For once, we'd wrap the listener to remove itself after first call
    // Simplified implementation for now
    return __runtime_events_on(emitter, event, listener);
}

bool __runtime_events_off(void* emitter, const char* event, void* listener) {
    if (!emitter || !event) return false;
    
    EventEmitter* ee = static_cast<EventEmitter*>(emitter);
    std::lock_guard<std::mutex> lock(ee->listeners_mutex);
    
    auto it = ee->listeners.find(event);
    if (it != ee->listeners.end()) {
        auto& vec = it->second;
        vec.erase(std::remove(vec.begin(), vec.end(), listener), vec.end());
        return true;
    }
    return false;
}

bool __runtime_events_emit(void* emitter, const char* event, void* args) {
    if (!emitter || !event) return false;
    
    EventEmitter* ee = static_cast<EventEmitter*>(emitter);
    std::lock_guard<std::mutex> lock(ee->listeners_mutex);
    
    auto it = ee->listeners.find(event);
    if (it != ee->listeners.end()) {
        // Execute all listeners - would need proper callback mechanism
        for (void* listener : it->second) {
            // Call listener function with args
        }
        return true;
    }
    return false;
}

void* __runtime_events_listeners(void* emitter, const char* event) {
    if (!emitter || !event) return __array_create(0);
    
    EventEmitter* ee = static_cast<EventEmitter*>(emitter);
    std::lock_guard<std::mutex> lock(ee->listeners_mutex);
    
    void* result = __array_create(0);
    auto it = ee->listeners.find(event);
    if (it != ee->listeners.end()) {
        for (void* listener : it->second) {
            __array_push(result, reinterpret_cast<int64_t>(listener));
        }
    }
    return result;
}

int64_t __runtime_events_listener_count(void* emitter, const char* event) {
    if (!emitter || !event) return 0;
    
    EventEmitter* ee = static_cast<EventEmitter*>(emitter);
    std::lock_guard<std::mutex> lock(ee->listeners_mutex);
    
    auto it = ee->listeners.find(event);
    return it != ee->listeners.end() ? static_cast<int64_t>(it->second.size()) : 0;
}

// Stream syscalls - Node.js-style streams
struct Stream {
    bool readable;
    bool writable;
    bool ended;
    EventEmitter* events;
    
    Stream(bool r, bool w) : readable(r), writable(w), ended(false) {
        events = static_cast<EventEmitter*>(__runtime_events_create_emitter());
    }
};

void* __runtime_stream_create_readable(void* options) {
    return new Stream(true, false);
}

void* __runtime_stream_create_writable(void* options) {
    return new Stream(false, true);
}

void* __runtime_stream_create_duplex(void* options) {
    return new Stream(true, true);
}

void* __runtime_stream_create_transform(void* options) {
    // Transform streams are duplex with data transformation
    return new Stream(true, true);
}

bool __runtime_stream_pipe(void* readable, void* writable, void* options) {
    if (!readable || !writable) return false;
    
    Stream* src = static_cast<Stream*>(readable);
    Stream* dest = static_cast<Stream*>(writable);
    
    if (!src->readable || !dest->writable) return false;
    
    // Set up piping - would need proper stream data handling
    return true;
}

// Async file operations that return promises
void* __runtime_fs_open_async(const char* path, const char* flags, int64_t mode) {
    // Return promise object that resolves with file descriptor
    // For now, perform synchronous operation and wrap in resolved promise
    int64_t fd = __runtime_fs_open(path, flags, mode);
    
    // Would create actual promise object here
    return reinterpret_cast<void*>(fd);
}

void* __runtime_fs_read_async(int64_t fd, void* buffer, int64_t size) {
    // Return promise object
    int64_t bytes_read = __runtime_fs_read(fd, buffer, size);
    return reinterpret_cast<void*>(bytes_read);
}

void* __runtime_fs_write_async(int64_t fd, const void* buffer, int64_t size) {
    // Return promise object
    int64_t bytes_written = __runtime_fs_write(fd, buffer, size);
    return reinterpret_cast<void*>(bytes_written);
}

void* __runtime_fs_close_async(int64_t fd) {
    // Return promise object
    int64_t result = __runtime_fs_close(fd);
    return reinterpret_cast<void*>(result);
}

// Memory management syscalls
void* __runtime_mem_alloc(int64_t size) {
    return malloc(static_cast<size_t>(size));
}

void __runtime_mem_free(void* ptr) {
    free(ptr);
}

void* __runtime_mem_realloc(void* ptr, int64_t size) {
    return realloc(ptr, static_cast<size_t>(size));
}

int64_t __runtime_mem_size(void* ptr) {
    // Would need malloc implementation that tracks sizes
    return 0; // Placeholder
}

void __runtime_gc_collect() {
    // Trigger garbage collection - would integrate with GoTS GC
}

int64_t __runtime_gc_heap_size() {
    // Return heap size
    return 0; // Placeholder
}

int64_t __runtime_gc_heap_used() {
    // Return used heap
    return 0; // Placeholder
}

// Error syscalls
void* __runtime_error_create(const char* message) {
    return __string_create(message ? message : "");
}

void* __runtime_error_stack_trace() {
    // Return stack trace array
    return __array_create(0);
}

void __runtime_error_capture_stack_trace(void* error) {
    // Capture stack trace for error object
}

// Zlib syscalls - compression/decompression
void* __runtime_zlib_deflate(void* buffer, void* options) {
    // Compress buffer using deflate algorithm
    // Would need zlib integration
    return buffer; // Placeholder
}

void* __runtime_zlib_inflate(void* buffer, void* options) {
    // Decompress buffer using inflate algorithm
    return buffer; // Placeholder
}

void* __runtime_zlib_gzip(void* buffer, void* options) {
    // Compress buffer using gzip
    return buffer; // Placeholder
}

void* __runtime_zlib_gunzip(void* buffer, void* options) {
    // Decompress gzip buffer
    return buffer; // Placeholder
}

// VM syscalls - code execution in different contexts
void* __runtime_vm_create_context(void* sandbox) {
    // Create VM execution context
    return nullptr; // Placeholder
}

void* __runtime_vm_run_in_context(const char* code, void* context) {
    // Execute code in context
    return __runtime_eval(code);
}

void* __runtime_vm_run_in_new_context(const char* code, void* sandbox) {
    // Execute code in new context
    return __runtime_eval(code);
}

void* __runtime_vm_run_in_this_context(const char* code) {
    // Execute code in current context
    return __runtime_eval(code);
}

// Global storage for managed Lock objects
namespace {
    std::vector<std::shared_ptr<Lock>> managed_locks;
    std::mutex locks_mutex;
}

// Lock syscalls - thread-safe locking primitives
void* __runtime_lock_create() {
    // Create a new Lock object and store it in managed storage
    try {
        std::lock_guard<std::mutex> guard(locks_mutex);
        auto lock = LockFactory::create_lock();
        void* raw_ptr = lock.get();
        managed_locks.push_back(lock);
        return raw_ptr; // Return raw pointer for runtime use
    } catch (const std::exception& e) {
        std::cerr << "Error creating lock: " << e.what() << std::endl;
        return nullptr;
    }
}

void __runtime_lock_lock(void* lock_ptr) {
    if (!lock_ptr) return;
    
    try {
        auto* lock = static_cast<Lock*>(lock_ptr);
        lock->lock();
    } catch (const std::exception& e) {
        std::cerr << "Error locking: " << e.what() << std::endl;
    }
}

void __runtime_lock_unlock(void* lock_ptr) {
    if (!lock_ptr) return;
    
    try {
        auto* lock = static_cast<Lock*>(lock_ptr);
        lock->unlock();
    } catch (const std::exception& e) {
        std::cerr << "Error unlocking: " << e.what() << std::endl;
    }
}

bool __runtime_lock_try_lock(void* lock_ptr) {
    if (!lock_ptr) return false;
    
    try {
        auto* lock = static_cast<Lock*>(lock_ptr);
        return lock->try_lock();
    } catch (const std::exception& e) {
        std::cerr << "Error trying lock: " << e.what() << std::endl;
        return false;
    }
}

bool __runtime_lock_try_lock_for(void* lock_ptr, int64_t timeout_ms) {
    if (!lock_ptr) return false;
    
    try {
        auto* lock = static_cast<Lock*>(lock_ptr);
        return lock->try_lock_for(std::chrono::milliseconds(timeout_ms));
    } catch (const std::exception& e) {
        std::cerr << "Error trying lock with timeout: " << e.what() << std::endl;
        return false;
    }
}

bool __runtime_lock_is_locked_by_current(void* lock_ptr) {
    if (!lock_ptr) return false;
    
    try {
        auto* lock = static_cast<Lock*>(lock_ptr);
        return lock->is_locked_by_current();
    } catch (const std::exception& e) {
        std::cerr << "Error checking lock ownership: " << e.what() << std::endl;
        return false;
    }
}

// Additional runtime functions for GoTS-specific features
void* __runtime_go_spawn(void* func, void* args) {
    // Spawn goroutine
    return nullptr; // Placeholder
}

void* __runtime_go_spawn_with_scope(void* func, void* scope) {
    // Spawn goroutine with lexical scope
    return nullptr; // Placeholder
}

void* __runtime_go_current_id() {
    // Get current goroutine ID
    return reinterpret_cast<void*>(1); // Placeholder
}

// Module/require system
void* __runtime_module_load(const char* path) {
    // Load module from path
    return nullptr; // Placeholder
}

void* __runtime_module_resolve(const char* request, void* options) {
    // Resolve module path
    return __string_create(request);
}

void* __runtime_module_create_require(const char* filename) {
    // Create require function for module
    return nullptr; // Placeholder
}

// JIT and eval functions
void* __runtime_compile(const char* code, const char* filename) {
    // Compile GoTS code
    return nullptr; // Placeholder
}

void* __runtime_eval(const char* code) {
    // Evaluate GoTS code
    return nullptr; // Placeholder
}

void* __runtime_jit_stats() {
    // Return JIT compilation statistics
    return __array_create(0);
}

void __runtime_jit_optimize(void* func) {
    // Optimize function
}

// Lock syscalls
void* __runtime_lock_create();
void __runtime_lock_lock(void* lock_ptr);
void __runtime_lock_unlock(void* lock_ptr);
bool __runtime_lock_try_lock(void* lock_ptr);
bool __runtime_lock_try_lock_for(void* lock_ptr, int64_t timeout_ms);
bool __runtime_lock_is_locked_by_current(void* lock_ptr);

} // extern "C"

// Initialize runtime object (C++ function, not extern "C")
void initialize_runtime_object() {
    if (global_runtime) return; // Already initialized
    
    global_runtime = new RuntimeObject();
    
    // Initialize time object function pointers
    global_runtime->time.now_millis = reinterpret_cast<void*>(__runtime_time_now_millis);
    global_runtime->time.now_nanos = reinterpret_cast<void*>(__runtime_time_now_nanos);
    global_runtime->time.timezone_offset = reinterpret_cast<void*>(__runtime_time_timezone_offset);
    global_runtime->time.sleep = reinterpret_cast<void*>(__runtime_time_sleep_millis);
    global_runtime->time.sleep_nanos = reinterpret_cast<void*>(__runtime_time_sleep_nanos);
    
    // Initialize process object function pointers
    global_runtime->process.pid = reinterpret_cast<void*>(__runtime_process_pid);
    global_runtime->process.ppid = reinterpret_cast<void*>(__runtime_process_ppid);
    global_runtime->process.uid = reinterpret_cast<void*>(__runtime_process_uid);
    global_runtime->process.gid = reinterpret_cast<void*>(__runtime_process_gid);
    global_runtime->process.cwd = reinterpret_cast<void*>(__runtime_process_cwd);
    global_runtime->process.chdir = reinterpret_cast<void*>(__runtime_process_chdir);
    global_runtime->process.exit = reinterpret_cast<void*>(__runtime_process_exit);
    global_runtime->process.argv = reinterpret_cast<void*>(__runtime_process_argv);
    global_runtime->process.platform = reinterpret_cast<void*>(__runtime_process_platform);
    global_runtime->process.arch = reinterpret_cast<void*>(__runtime_process_arch);
    global_runtime->process.version = reinterpret_cast<void*>(__runtime_process_version);
    global_runtime->process.memoryUsage = reinterpret_cast<void*>(__runtime_process_memory_usage);
    global_runtime->process.cpuUsage = reinterpret_cast<void*>(__runtime_process_cpu_usage);
    
    // Initialize fs object function pointers
    global_runtime->fs.open = reinterpret_cast<void*>(__runtime_fs_open);
    global_runtime->fs.close = reinterpret_cast<void*>(__runtime_fs_close);
    global_runtime->fs.read = reinterpret_cast<void*>(__runtime_fs_read);
    global_runtime->fs.write = reinterpret_cast<void*>(__runtime_fs_write);
    global_runtime->fs.exists = reinterpret_cast<void*>(__runtime_fs_exists);
    global_runtime->fs.stat = reinterpret_cast<void*>(__runtime_fs_size); // Using size as stat for now
    global_runtime->fs.mkdir = reinterpret_cast<void*>(__runtime_fs_mkdir);
    global_runtime->fs.rmdir = reinterpret_cast<void*>(__runtime_fs_rmdir);
    global_runtime->fs.unlink = reinterpret_cast<void*>(__runtime_fs_unlink);
    global_runtime->fs.rename = reinterpret_cast<void*>(__runtime_fs_rename);
    global_runtime->fs.readdir = reinterpret_cast<void*>(__runtime_fs_readdir);
    
    // Initialize os object function pointers
    global_runtime->os.hostname = reinterpret_cast<void*>(__runtime_os_hostname);
    global_runtime->os.type = reinterpret_cast<void*>(__runtime_os_type);
    global_runtime->os.platform = reinterpret_cast<void*>(__runtime_process_platform); // Reuse
    global_runtime->os.release = reinterpret_cast<void*>(__runtime_os_release);
    global_runtime->os.arch = reinterpret_cast<void*>(__runtime_process_arch); // Reuse
    global_runtime->os.tmpdir = reinterpret_cast<void*>(__runtime_os_tmpdir);
    global_runtime->os.homedir = reinterpret_cast<void*>(__runtime_os_homedir);
    global_runtime->os.uptime = reinterpret_cast<void*>(__runtime_os_uptime);
    global_runtime->os.freemem = reinterpret_cast<void*>(__runtime_os_freemem);
    global_runtime->os.totalmem = reinterpret_cast<void*>(__runtime_os_totalmem);
    
    // Initialize lock object function pointers
    global_runtime->lock.create = reinterpret_cast<void*>(__runtime_lock_create);
    
    // Register all methods for JIT optimization
    runtime_method_registry["time.now"] = {"time.now", global_runtime->time.now_millis, false, 0};
    runtime_method_registry["time.nowNanos"] = {"time.nowNanos", global_runtime->time.now_nanos, false, 0};
    runtime_method_registry["process.pid"] = {"process.pid", global_runtime->process.pid, false, 0};
    runtime_method_registry["process.cwd"] = {"process.cwd", global_runtime->process.cwd, false, 0};
    runtime_method_registry["lock.create"] = {"lock.create", global_runtime->lock.create, false, 0};
    // Add more as needed...
}

// Simple test function to verify calling convention
int64_t __runtime_test_simple() {
    std::cout << "DEBUG: __runtime_test_simple called successfully!" << std::endl;
    return 42;
}

extern "C" {
// Registration function called at startup
void __runtime_register_global() {
    initialize_runtime_object();
    
    // Register all runtime syscall functions in the JIT function registry
    extern uint16_t __register_function_fast(void* func_ptr, uint16_t arg_count, uint8_t calling_convention);
    
    // Time functions
    __register_function_fast(reinterpret_cast<void*>(__runtime_time_now_millis), 0, 0);
    __register_function_fast(reinterpret_cast<void*>(__runtime_time_now_nanos), 0, 0);
    __register_function_fast(reinterpret_cast<void*>(__runtime_time_timezone_offset), 0, 0);
    __register_function_fast(reinterpret_cast<void*>(__runtime_time_sleep_millis), 1, 0);
    
    // Process functions
    __register_function_fast(reinterpret_cast<void*>(__runtime_process_pid), 0, 0);
    __register_function_fast(reinterpret_cast<void*>(__runtime_process_cwd), 0, 0);
    __register_function_fast(reinterpret_cast<void*>(__runtime_process_platform), 0, 0);
    __register_function_fast(reinterpret_cast<void*>(__runtime_process_arch), 0, 0);
    
    // File system functions
    __register_function_fast(reinterpret_cast<void*>(__runtime_fs_open), 2, 0);
    __register_function_fast(reinterpret_cast<void*>(__runtime_fs_close), 1, 0);
    __register_function_fast(reinterpret_cast<void*>(__runtime_fs_exists), 1, 0);
    __register_function_fast(reinterpret_cast<void*>(__runtime_fs_readdir), 1, 0);
    
    // Network functions
    __register_function_fast(reinterpret_cast<void*>(__runtime_net_socket), 3, 0);
    __register_function_fast(reinterpret_cast<void*>(__runtime_net_bind), 3, 0);
    __register_function_fast(reinterpret_cast<void*>(__runtime_net_listen), 2, 0);
    __register_function_fast(reinterpret_cast<void*>(__runtime_dns_lookup), 1, 0);
    
    // Buffer functions
    __register_function_fast(reinterpret_cast<void*>(__runtime_buffer_alloc), 1, 0);
    __register_function_fast(reinterpret_cast<void*>(__runtime_buffer_from_string), 1, 0);
    __register_function_fast(reinterpret_cast<void*>(__runtime_buffer_to_string), 1, 0);
    
    // Path functions
    __register_function_fast(reinterpret_cast<void*>(__runtime_path_basename), 1, 0);
    __register_function_fast(reinterpret_cast<void*>(__runtime_path_dirname), 1, 0);
    __register_function_fast(reinterpret_cast<void*>(__runtime_path_extname), 1, 0);
    __register_function_fast(reinterpret_cast<void*>(__runtime_path_normalize), 1, 0);
    
    // OS functions
    __register_function_fast(reinterpret_cast<void*>(__runtime_os_hostname), 0, 0);
    __register_function_fast(reinterpret_cast<void*>(__runtime_os_type), 0, 0);
    __register_function_fast(reinterpret_cast<void*>(__runtime_os_uptime), 0, 0);
    __register_function_fast(reinterpret_cast<void*>(__runtime_os_freemem), 0, 0);
    
    // Crypto functions
    __register_function_fast(reinterpret_cast<void*>(__runtime_crypto_random_bytes), 1, 0);
    
    // Timer functions
    __register_function_fast(reinterpret_cast<void*>(__runtime_timer_set_timeout), 2, 0);
    __register_function_fast(reinterpret_cast<void*>(__runtime_timer_clear_timeout), 1, 0);
    
    // Lock functions
    __register_function_fast(reinterpret_cast<void*>(__runtime_lock_create), 0, 0);
    __register_function_fast(reinterpret_cast<void*>(__runtime_lock_lock), 1, 0);
    __register_function_fast(reinterpret_cast<void*>(__runtime_lock_unlock), 1, 0);
    __register_function_fast(reinterpret_cast<void*>(__runtime_lock_try_lock), 1, 0);
    __register_function_fast(reinterpret_cast<void*>(__runtime_lock_try_lock_for), 2, 0);
    __register_function_fast(reinterpret_cast<void*>(__runtime_lock_is_locked_by_current), 1, 0);
    
    // Math functions
    __register_function_fast(reinterpret_cast<void*>(__runtime_math_random), 0, 0);
    
    // Test function
    __register_function_fast(reinterpret_cast<void*>(__runtime_test_simple), 0, 0);
    
    // Legacy goroutine functions removed - using fast spawn system
    
    std::cout << "DEBUG: All runtime syscalls registered!" << std::endl;
}
} // extern "C"

} // namespace gots