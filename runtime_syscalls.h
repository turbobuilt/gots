#pragma once

#include <cstdint>

namespace gots {

// Runtime syscall interface - exposes all system APIs needed for Node.js compatibility
// This provides the global 'runtime' object in GoTS with comprehensive system access
// All high-level implementations (Date, fs, net, etc.) are built in GoTS using these primitives

extern "C" {
    // PERFORMANCE NOTE: These syscalls are designed for flat access patterns
    // The GoTS compiler can optimize nested property access (runtime.time.now)
    // to direct function calls, eliminating the performance penalty.
    // 
    // For hot paths, the JIT will inline these calls completely.
    
    // Time and Date syscalls
    int64_t __runtime_time_now_millis();
    int64_t __runtime_time_now_nanos();
    int64_t __runtime_time_timezone_offset();
    int64_t __runtime_time_daylight_saving();
    void __runtime_time_sleep_millis(int64_t millis);
    void __runtime_time_sleep_nanos(int64_t nanos);
    
    // Process syscalls
    int64_t __runtime_process_pid();
    int64_t __runtime_process_ppid();
    int64_t __runtime_process_uid();
    int64_t __runtime_process_gid();
    void* __runtime_process_cwd();
    bool __runtime_process_chdir(const char* path);
    void __runtime_process_exit(int64_t code);
    void* __runtime_process_argv();
    void* __runtime_process_env_get(const char* key);
    bool __runtime_process_env_set(const char* key, const char* value);
    bool __runtime_process_env_delete(const char* key);
    void* __runtime_process_env_keys();
    int64_t __runtime_process_memory_usage();
    double __runtime_process_cpu_usage();
    void* __runtime_process_platform();
    void* __runtime_process_arch();
    void* __runtime_process_version();
    
    // File system syscalls
    int64_t __runtime_fs_open(const char* path, const char* flags, int64_t mode);
    int64_t __runtime_fs_close(int64_t fd);
    int64_t __runtime_fs_read(int64_t fd, void* buffer, int64_t size);
    int64_t __runtime_fs_write(int64_t fd, const void* buffer, int64_t size);
    int64_t __runtime_fs_seek(int64_t fd, int64_t offset, int64_t whence);
    bool __runtime_fs_exists(const char* path);
    bool __runtime_fs_is_file(const char* path);
    bool __runtime_fs_is_directory(const char* path);
    bool __runtime_fs_is_symlink(const char* path);
    int64_t __runtime_fs_size(const char* path);
    int64_t __runtime_fs_mtime(const char* path);
    int64_t __runtime_fs_atime(const char* path);
    int64_t __runtime_fs_ctime(const char* path);
    bool __runtime_fs_chmod(const char* path, int64_t mode);
    bool __runtime_fs_chown(const char* path, int64_t uid, int64_t gid);
    bool __runtime_fs_mkdir(const char* path, int64_t mode);
    bool __runtime_fs_rmdir(const char* path);
    bool __runtime_fs_unlink(const char* path);
    bool __runtime_fs_rename(const char* from, const char* to);
    bool __runtime_fs_symlink(const char* target, const char* path);
    void* __runtime_fs_readlink(const char* path);
    void* __runtime_fs_realpath(const char* path);
    void* __runtime_fs_readdir(const char* path);
    bool __runtime_fs_copy(const char* from, const char* to);
    
    // Async file system (returns promise handles)
    void* __runtime_fs_open_async(const char* path, const char* flags, int64_t mode);
    void* __runtime_fs_read_async(int64_t fd, void* buffer, int64_t size);
    void* __runtime_fs_write_async(int64_t fd, const void* buffer, int64_t size);
    void* __runtime_fs_close_async(int64_t fd);
    
    // Network syscalls
    int64_t __runtime_net_socket(int64_t domain, int64_t type, int64_t protocol);
    bool __runtime_net_bind(int64_t sockfd, const char* address, int64_t port);
    bool __runtime_net_listen(int64_t sockfd, int64_t backlog);
    int64_t __runtime_net_accept(int64_t sockfd, void* address);
    bool __runtime_net_connect(int64_t sockfd, const char* address, int64_t port);
    int64_t __runtime_net_send(int64_t sockfd, const void* buffer, int64_t size, int64_t flags);
    int64_t __runtime_net_recv(int64_t sockfd, void* buffer, int64_t size, int64_t flags);
    bool __runtime_net_close(int64_t sockfd);
    bool __runtime_net_shutdown(int64_t sockfd, int64_t how);
    bool __runtime_net_setsockopt(int64_t sockfd, int64_t level, int64_t optname, const void* optval, int64_t optlen);
    bool __runtime_net_getsockopt(int64_t sockfd, int64_t level, int64_t optname, void* optval, int64_t* optlen);
    void* __runtime_net_gethostbyname(const char* hostname);
    void* __runtime_net_gethostbyaddr(const char* addr, int64_t len, int64_t type);
    
    // DNS syscalls
    void* __runtime_dns_lookup(const char* hostname, int64_t family);
    void* __runtime_dns_reverse(const char* ip);
    void* __runtime_dns_resolve4(const char* hostname);
    void* __runtime_dns_resolve6(const char* hostname);
    void* __runtime_dns_resolveMx(const char* hostname);
    void* __runtime_dns_resolveTxt(const char* hostname);
    void* __runtime_dns_resolveSrv(const char* hostname);
    void* __runtime_dns_resolveNs(const char* hostname);
    void* __runtime_dns_resolveCname(const char* hostname);
    
    // HTTP syscalls (basic)
    void* __runtime_http_request(const char* method, const char* url, void* headers, const void* body, int64_t body_size);
    void* __runtime_http_create_server(void* handler);
    bool __runtime_http_server_listen(void* server, int64_t port, const char* host);
    
    // Crypto syscalls
    void* __runtime_crypto_random_bytes(int64_t size);
    void* __runtime_crypto_pbkdf2(const char* password, const char* salt, int64_t iterations, int64_t keylen, const char* digest);
    void* __runtime_crypto_create_hash(const char* algorithm);
    void* __runtime_crypto_create_hmac(const char* algorithm, const char* key);
    void* __runtime_crypto_hash_update(void* hash, const void* data, int64_t size);
    void* __runtime_crypto_hash_digest(void* hash, const char* encoding);
    void* __runtime_crypto_sign(const char* algorithm, const void* data, int64_t size, const char* key);
    void* __runtime_crypto_verify(const char* algorithm, const void* data, int64_t size, const char* key, const void* signature, int64_t sig_size);
    
    // Buffer/Binary syscalls
    void* __runtime_buffer_alloc(int64_t size);
    void* __runtime_buffer_from_string(const char* str, const char* encoding);
    void* __runtime_buffer_from_array(void* array);
    void* __runtime_buffer_concat(void* list);
    int64_t __runtime_buffer_length(void* buffer);
    void* __runtime_buffer_slice(void* buffer, int64_t start, int64_t end);
    bool __runtime_buffer_equals(void* buf1, void* buf2);
    int64_t __runtime_buffer_compare(void* buf1, void* buf2);
    void* __runtime_buffer_to_string(void* buffer, const char* encoding);
    
    // Child process syscalls
    void* __runtime_child_spawn(const char* command, void* args, void* options);
    void* __runtime_child_exec(const char* command, void* options);
    bool __runtime_child_kill(int64_t pid, int64_t signal);
    
    // Timer syscalls
    int64_t __runtime_timer_set_timeout(void* callback, int64_t delay);
    int64_t __runtime_timer_set_interval(void* callback, int64_t delay);
    int64_t __runtime_timer_set_immediate(void* callback);
    bool __runtime_timer_clear_timeout(int64_t id);
    bool __runtime_timer_clear_interval(int64_t id);
    bool __runtime_timer_clear_immediate(int64_t id);
    void __runtime_timer_wait_all();
    void __runtime_timer_cleanup();
    void __runtime_process_deferred_timers();
    
    // Event emitter syscalls
    void* __runtime_events_create_emitter();
    bool __runtime_events_on(void* emitter, const char* event, void* listener);
    bool __runtime_events_once(void* emitter, const char* event, void* listener);
    bool __runtime_events_off(void* emitter, const char* event, void* listener);
    bool __runtime_events_emit(void* emitter, const char* event, void* args);
    void* __runtime_events_listeners(void* emitter, const char* event);
    int64_t __runtime_events_listener_count(void* emitter, const char* event);
    
    // Stream syscalls
    void* __runtime_stream_create_readable(void* options);
    void* __runtime_stream_create_writable(void* options);
    void* __runtime_stream_create_duplex(void* options);
    void* __runtime_stream_create_transform(void* options);
    bool __runtime_stream_pipe(void* readable, void* writable, void* options);
    
    // OS info syscalls
    void* __runtime_os_hostname();
    void* __runtime_os_type();
    void* __runtime_os_platform();
    void* __runtime_os_release();
    void* __runtime_os_arch();
    int64_t __runtime_os_uptime();
    int64_t __runtime_os_freemem();
    int64_t __runtime_os_totalmem();
    void* __runtime_os_cpus();
    void* __runtime_os_networkInterfaces();
    void* __runtime_os_tmpdir();
    void* __runtime_os_homedir();
    
    // Console/TTY syscalls
    bool __runtime_tty_is_tty(int64_t fd);
    void* __runtime_tty_get_window_size();
    bool __runtime_tty_set_raw_mode(int64_t fd, bool enable);
    void* __runtime_readline_create_interface(void* input, void* output);
    
    // Cluster/Worker syscalls
    bool __runtime_cluster_is_master();
    bool __runtime_cluster_is_worker();
    void* __runtime_cluster_fork();
    void* __runtime_cluster_workers();
    void* __runtime_worker_create(const char* filename, void* options);
    
    // Module/Require syscalls
    void* __runtime_module_load(const char* path);
    void* __runtime_module_resolve(const char* request, void* options);
    void* __runtime_module_create_require(const char* filename);
    
    // URL syscalls
    void* __runtime_url_parse(const char* url, bool parse_query);
    void* __runtime_url_format(void* url_object);
    void* __runtime_url_resolve(const char* from, const char* to);
    
    // Query string syscalls
    void* __runtime_querystring_parse(const char* str, const char* sep, const char* eq);
    void* __runtime_querystring_stringify(void* obj, const char* sep, const char* eq);
    
    // Path syscalls
    void* __runtime_path_basename(const char* path, const char* ext);
    void* __runtime_path_dirname(const char* path);
    void* __runtime_path_extname(const char* path);
    void* __runtime_path_format(void* path_object);
    void* __runtime_path_parse(const char* path);
    bool __runtime_path_is_absolute(const char* path);
    void* __runtime_path_join(void* paths);
    void* __runtime_path_normalize(const char* path);
    void* __runtime_path_relative(const char* from, const char* to);
    void* __runtime_path_resolve(void* paths);
    char __runtime_path_sep();
    char __runtime_path_delimiter();
    
    // Util syscalls
    void* __runtime_util_format(const char* format, void* args);
    void* __runtime_util_inspect(void* object, void* options);
    bool __runtime_util_is_array(void* value);
    bool __runtime_util_is_date(void* value);
    bool __runtime_util_is_error(void* value);
    bool __runtime_util_is_function(void* value);
    bool __runtime_util_is_null(void* value);
    bool __runtime_util_is_number(void* value);
    bool __runtime_util_is_object(void* value);
    bool __runtime_util_is_primitive(void* value);
    bool __runtime_util_is_regexp(void* value);
    bool __runtime_util_is_string(void* value);
    bool __runtime_util_is_symbol(void* value);
    bool __runtime_util_is_undefined(void* value);
    
    // Performance syscalls
    int64_t __runtime_perf_now();
    void __runtime_perf_mark(const char* name);
    void __runtime_perf_measure(const char* name, const char* start_mark, const char* end_mark);
    void* __runtime_perf_get_entries();
    void* __runtime_perf_get_entries_by_name(const char* name);
    void* __runtime_perf_get_entries_by_type(const char* type);
    
    // Zlib syscalls
    void* __runtime_zlib_deflate(void* buffer, void* options);
    void* __runtime_zlib_inflate(void* buffer, void* options);
    void* __runtime_zlib_gzip(void* buffer, void* options);
    void* __runtime_zlib_gunzip(void* buffer, void* options);
    
    // V8/VM syscalls
    void* __runtime_vm_create_context(void* sandbox);
    void* __runtime_vm_run_in_context(const char* code, void* context);
    void* __runtime_vm_run_in_new_context(const char* code, void* sandbox);
    void* __runtime_vm_run_in_this_context(const char* code);
    
    // Assert syscalls
    void __runtime_assert(bool condition, const char* message);
    void __runtime_assert_equal(void* actual, void* expected, const char* message);
    void __runtime_assert_not_equal(void* actual, void* expected, const char* message);
    void __runtime_assert_deep_equal(void* actual, void* expected, const char* message);
    void __runtime_assert_throws(void* function, void* error, const char* message);
    
    // Debugger syscalls
    void __runtime_debugger_break();
    bool __runtime_debugger_attached();
    
    // WASI syscalls (for future WebAssembly support)
    void* __runtime_wasi_snapshot_preview1();
    
    // Memory management syscalls
    void* __runtime_mem_alloc(int64_t size);
    void __runtime_mem_free(void* ptr);
    void* __runtime_mem_realloc(void* ptr, int64_t size);
    int64_t __runtime_mem_size(void* ptr);
    void __runtime_gc_collect();
    int64_t __runtime_gc_heap_size();
    int64_t __runtime_gc_heap_used();
    
    // Error syscalls
    void* __runtime_error_create(const char* message);
    void* __runtime_error_stack_trace();
    void __runtime_error_capture_stack_trace(void* error);
    
    // Intl syscalls
    void* __runtime_intl_collator(const char* locales, void* options);
    void* __runtime_intl_date_time_format(const char* locales, void* options);
    void* __runtime_intl_number_format(const char* locales, void* options);
    
    // WeakMap/WeakSet syscalls
    void* __runtime_weakmap_create();
    void* __runtime_weakset_create();
    bool __runtime_weakmap_set(void* map, void* key, void* value);
    void* __runtime_weakmap_get(void* map, void* key);
    bool __runtime_weakmap_has(void* map, void* key);
    bool __runtime_weakmap_delete(void* map, void* key);
    
    // Atomics syscalls
    int64_t __runtime_atomics_add(void* array, int64_t index, int64_t value);
    int64_t __runtime_atomics_and(void* array, int64_t index, int64_t value);
    int64_t __runtime_atomics_compare_exchange(void* array, int64_t index, int64_t expected, int64_t replacement);
    int64_t __runtime_atomics_exchange(void* array, int64_t index, int64_t value);
    int64_t __runtime_atomics_load(void* array, int64_t index);
    int64_t __runtime_atomics_or(void* array, int64_t index, int64_t value);
    int64_t __runtime_atomics_store(void* array, int64_t index, int64_t value);
    int64_t __runtime_atomics_sub(void* array, int64_t index, int64_t value);
    int64_t __runtime_atomics_xor(void* array, int64_t index, int64_t value);
    bool __runtime_atomics_is_lock_free(int64_t size);
    int64_t __runtime_atomics_wait(void* array, int64_t index, int64_t value, int64_t timeout);
    int64_t __runtime_atomics_notify(void* array, int64_t index, int64_t count);
    
    // SharedArrayBuffer syscalls
    void* __runtime_shared_array_buffer_create(int64_t size);
    int64_t __runtime_shared_array_buffer_byte_length(void* buffer);
    void* __runtime_shared_array_buffer_slice(void* buffer, int64_t start, int64_t end);
    
    // Registration function to create the global runtime object
    void __runtime_register_global();
    
    // Existing runtime functions exposed to GoTS
    // Goroutine management
    void* __runtime_go_spawn(void* func, void* args);
    void* __runtime_go_spawn_with_scope(void* func, void* scope);
    void* __runtime_go_current_id();
    
    // Promise utilities (beyond standard Promise methods)
    void* __runtime_promise_all_settled(void* promises);
    void* __runtime_promise_race(void* promises);
    void* __runtime_promise_any(void* promises);
    
    // Type system and introspection
    int64_t __runtime_typeof(void* value);
    bool __runtime_instanceof(void* value, void* constructor);
    void* __runtime_get_prototype(void* object);
    void* __runtime_get_constructor(void* object);
    
    // JIT and eval
    void* __runtime_compile(const char* code, const char* filename);
    void* __runtime_eval(const char* code);
    void* __runtime_jit_stats();
    void __runtime_jit_optimize(void* func);
    
    // Array and TypedArray utilities
    void* __runtime_array_from(void* iterable);
    bool __runtime_array_is_array(void* value);
    void* __runtime_typed_array_from(void* source, int64_t type);
    
    // String pool access
    void* __runtime_intern_string(const char* str);
    int64_t __runtime_string_pool_size();
    void __runtime_string_pool_clear();
    
    // Console extensions
    void __runtime_console_trace();
    void __runtime_console_profile(const char* label);
    void __runtime_console_profile_end(const char* label);
    
    // Object utilities
    void* __runtime_object_keys(void* object);
    void* __runtime_object_values(void* object);
    void* __runtime_object_entries(void* object);
    void* __runtime_object_freeze(void* object);
    void* __runtime_object_seal(void* object);
    bool __runtime_object_is_frozen(void* object);
    bool __runtime_object_is_sealed(void* object);
    
    // Regex utilities
    void* __runtime_regex_escape(const char* str);
    
    // Math extensions
    double __runtime_math_random();
    void __runtime_math_random_seed(int64_t seed);
    
    // Lock syscalls - thread-safe locking primitives
    void* __runtime_lock_create();
    void __runtime_lock_lock(void* lock_ptr);
    void __runtime_lock_unlock(void* lock_ptr);
    bool __runtime_lock_try_lock(void* lock_ptr);
    bool __runtime_lock_try_lock_for(void* lock_ptr, int64_t timeout_ms);
    bool __runtime_lock_is_locked_by_current(void* lock_ptr);
    
    // Internal debugging
    void* __runtime_heap_snapshot();
    void* __runtime_cpu_profile(int64_t duration_ms);
    void __runtime_trace_start(const char* categories);
    void __runtime_trace_stop();
    void* __runtime_trace_events();
}

} // namespace gots