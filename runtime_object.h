#pragma once

#include <cstdint>
#include <unordered_map>
#include <string>

namespace gots {

// Runtime object structure designed for JIT optimization
// The JIT compiler recognizes these constant paths and converts them to direct function calls
// For example: runtime.time.now() -> __runtime_time_now_millis() with zero overhead

struct RuntimeObject {
    // Each sub-object is a constant structure with function pointers
    // The JIT can resolve these at compile time
    
    struct TimeObject {
        static constexpr const char* OBJECT_NAME = "time";
        
        // Function pointers stored as raw addresses for maximum performance
        void* now_millis;        // runtime.time.now() -> returns milliseconds
        void* now_nanos;         // runtime.time.nowNanos() -> returns nanoseconds  
        void* timezone_offset;   // runtime.time.timezoneOffset()
        void* sleep;            // runtime.time.sleep(ms)
        void* sleep_nanos;      // runtime.time.sleepNanos(ns)
    };
    
    struct ProcessObject {
        static constexpr const char* OBJECT_NAME = "process";
        
        void* pid;
        void* ppid;
        void* uid;
        void* gid;
        void* cwd;
        void* chdir;
        void* exit;
        void* argv;
        void* env;
        void* platform;
        void* arch;
        void* version;
        void* memoryUsage;
        void* cpuUsage;
    };
    
    struct FSObject {
        static constexpr const char* OBJECT_NAME = "fs";
        
        // Sync operations
        void* open;
        void* close;
        void* read;
        void* write;
        void* readFile;
        void* writeFile;
        void* exists;
        void* stat;
        void* mkdir;
        void* rmdir;
        void* unlink;
        void* rename;
        void* readdir;
        
        // Async operations (return promises)
        void* openAsync;
        void* readAsync;
        void* writeAsync;
        void* readFileAsync;
        void* writeFileAsync;
    };
    
    struct NetObject {
        static constexpr const char* OBJECT_NAME = "net";
        
        void* createServer;
        void* connect;
        void* socket;
        void* bind;
        void* listen;
        void* accept;
        void* send;
        void* recv;
    };
    
    struct CryptoObject {
        static constexpr const char* OBJECT_NAME = "crypto";
        
        void* randomBytes;
        void* createHash;
        void* createHmac;
        void* pbkdf2;
        void* sign;
        void* verify;
    };
    
    struct BufferObject {
        static constexpr const char* OBJECT_NAME = "buffer";
        
        void* alloc;
        void* from;
        void* concat;
        void* isBuffer;
    };
    
    struct OSObject {
        static constexpr const char* OBJECT_NAME = "os";
        
        void* hostname;
        void* type;
        void* platform;
        void* release;
        void* arch;
        void* cpus;
        void* networkInterfaces;
        void* homedir;
        void* tmpdir;
        void* uptime;
        void* freemem;
        void* totalmem;
    };
    
    struct PathObject {
        static constexpr const char* OBJECT_NAME = "path";
        
        void* basename;
        void* dirname;
        void* extname;
        void* join;
        void* resolve;
        void* relative;
        void* normalize;
        void* parse;
        void* format;
        void* isAbsolute;
        void* sep;
        void* delimiter;
    };
    
    struct ConsoleObject {
        static constexpr const char* OBJECT_NAME = "console";
        
        void* log;
        void* error;
        void* warn;
        void* info;
        void* debug;
        void* trace;
        void* time;
        void* timeEnd;
        void* profile;
        void* profileEnd;
    };
    
    struct JITObject {
        static constexpr const char* OBJECT_NAME = "jit";
        
        void* compile;
        void* optimize;
        void* stats;
        void* disassemble;
    };
    
    struct GCObject {
        static constexpr const char* OBJECT_NAME = "gc";
        
        void* collect;
        void* heapSize;
        void* heapUsed;
        void* nextGC;
    };
    
    struct LockObject {
        static constexpr const char* OBJECT_NAME = "lock";
        
        void* create;           // runtime.lock.create() -> new Lock()
    };
    
    // Runtime object layout - designed for cache efficiency
    TimeObject time;
    ProcessObject process;
    FSObject fs;
    NetObject net;
    CryptoObject crypto;
    BufferObject buffer;
    OSObject os;
    PathObject path;
    ConsoleObject console;
    JITObject jit;
    GCObject gc;
    LockObject lock;
    
    // Direct function pointers for frequently used operations
    void* eval;
    void* compile;
    void* typeof;
    void* instanceof;
    
    // Goroutine functions
    void* go;
    void* goMap;
};

// Global runtime instance - constant after initialization
extern RuntimeObject* global_runtime;

// Registration info for JIT optimization
struct RuntimeMethodInfo {
    const char* object_path;     // e.g., "time.now"
    void* function_pointer;      // Direct function pointer
    bool is_async;              // Returns a promise
    int arg_count;              // Expected argument count (-1 for variadic)
};

// Registry of all runtime methods for JIT optimization
extern std::unordered_map<std::string, RuntimeMethodInfo> runtime_method_registry;

// Initialize the runtime object and method registry
void initialize_runtime_object();

// Helper for JIT to resolve runtime calls at compile time
inline void* resolve_runtime_method(const char* object_name, const char* method_name) {
    // This function is designed to be called at JIT compile time
    // It returns the direct function pointer for inlining
    
    if (strcmp(object_name, "time") == 0) {
        if (strcmp(method_name, "now") == 0) return global_runtime->time.now_millis;
        if (strcmp(method_name, "nowNanos") == 0) return global_runtime->time.now_nanos;
        if (strcmp(method_name, "sleep") == 0) return global_runtime->time.sleep;
        // ... etc
    } else if (strcmp(object_name, "fs") == 0) {
        if (strcmp(method_name, "readFile") == 0) return global_runtime->fs.readFile;
        if (strcmp(method_name, "writeFile") == 0) return global_runtime->fs.writeFile;
        // ... etc
    } else if (strcmp(object_name, "lock") == 0) {
        if (strcmp(method_name, "create") == 0) return global_runtime->lock.create;
        // ... etc
    }
    // ... more objects
    
    return nullptr;
}

// Macro for JIT to use when generating code
// This allows the JIT to emit a direct CALL instruction instead of property lookups
#define RUNTIME_METHOD_ADDRESS(obj, method) \
    ((void*)&global_runtime->obj.method)

} // namespace gots