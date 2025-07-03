#pragma once

#include "runtime.h"
#include <vector>
#include <functional>
#include <future>
#include <type_traits>

namespace gots {

template<typename T>
class PromiseAll {
private:
    std::vector<std::shared_ptr<Promise>> promises;
    
public:
    PromiseAll() = default;
    
    PromiseAll(const std::vector<std::shared_ptr<Promise>>& promise_list) 
        : promises(promise_list) {}
    
    template<typename Container>
    static auto all(const Container& promises) -> std::future<std::vector<int>> {
        using ValueType = int;
        
        auto promise_ptr = std::make_shared<std::promise<std::vector<ValueType>>>();
        auto future = promise_ptr->get_future();
        
        if (promises.empty()) {
            promise_ptr->set_value(std::vector<ValueType>());
            return future;
        }
        
        auto results = std::make_shared<std::vector<ValueType>>(promises.size());
        auto completed_count = std::make_shared<std::atomic<size_t>>(0);
        auto total_count = promises.size();
        
        for (size_t i = 0; i < promises.size(); ++i) {
            promises[i]->then([promise_ptr, results, completed_count, total_count, i, promise = promises[i]]() {
                try {
                    (*results)[i] = promise->template await<ValueType>();
                    
                    if (completed_count->fetch_add(1) + 1 == total_count) {
                        promise_ptr->set_value(*results);
                    }
                } catch (...) {
                    promise_ptr->set_exception(std::current_exception());
                }
            });
        }
        
        return future;
    }
    
    template<typename F, typename Container>
    static auto goMap(const Container& items, F&& func) -> std::future<std::vector<decltype(func(items[0]))>> {
        using ResultType = decltype(func(items[0]));
        
        std::vector<std::shared_ptr<Promise>> promises;
        promises.reserve(items.size());
        
        auto& scheduler = GoroutineScheduler::instance();
        
        for (const auto& item : items) {
            auto promise = scheduler.spawn([func, item]() {
                return func(item);
            });
            promises.push_back(promise);
        }
        
        return all(promises);
    }
    
    template<typename T1, typename T2>
    static auto race(const std::vector<std::shared_ptr<Promise>>& promises) -> std::future<T1> {
        auto promise_ptr = std::make_shared<std::promise<T1>>();
        auto future = promise_ptr->get_future();
        auto resolved = std::make_shared<std::atomic<bool>>(false);
        
        if (promises.empty()) {
            promise_ptr->set_exception(std::make_exception_ptr(std::runtime_error("Promise.race with empty array")));
            return future;
        }
        
        for (auto& promise : promises) {
            promise->then([promise_ptr, resolved, promise]() {
                if (!resolved->exchange(true)) {
                    try {
                        T1 result = promise->template await<T1>();
                        promise_ptr->set_value(result);
                    } catch (...) {
                        promise_ptr->set_exception(std::current_exception());
                    }
                }
            });
        }
        
        return future;
    }
    
    template<typename F>
    static auto resolve(F&& value) -> std::shared_ptr<Promise> {
        auto promise = std::make_shared<Promise>();
        promise->resolve(std::forward<F>(value));
        return promise;
    }
    
    template<typename E>
    static auto reject(E&& error) -> std::shared_ptr<Promise> {
        auto promise = std::make_shared<Promise>();
        promise->resolve(std::forward<E>(error));
        return promise;
    }
    
    template<typename F>
    auto then(F&& callback) -> std::shared_ptr<Promise> {
        auto new_promise = std::make_shared<Promise>();
        
        auto all_future = all(promises);
        
        std::thread([new_promise, all_future = std::move(all_future), callback = std::forward<F>(callback)]() mutable {
            try {
                auto results = all_future.get();
                auto result = callback(results);
                new_promise->resolve(result);
            } catch (...) {
                new_promise->resolve(false);
            }
        }).detach();
        
        return new_promise;
    }
    
    template<typename F>
    auto catch_error(F&& error_handler) -> std::shared_ptr<Promise> {
        auto new_promise = std::make_shared<Promise>();
        
        auto all_future = all(promises);
        
        std::thread([new_promise, all_future = std::move(all_future), error_handler = std::forward<F>(error_handler)]() mutable {
            try {
                auto results = all_future.get();
                new_promise->resolve(results);
            } catch (...) {
                try {
                    auto result = error_handler(std::current_exception());
                    new_promise->resolve(result);
                } catch (...) {
                    new_promise->resolve(false);
                }
            }
        }).detach();
        
        return new_promise;
    }
};

template<typename Container>
auto promise_all(const Container& promises) {
    return PromiseAll<typename Container::value_type::element_type::value_type>::all(promises);
}

template<typename F, typename Container>
auto goMap(const Container& items, F&& func) {
    return PromiseAll<decltype(func(items[0]))>::goMap(items, std::forward<F>(func));
}

template<typename T>
auto promise_race(const std::vector<std::shared_ptr<Promise>>& promises) {
    return PromiseAll<T>::template race<T, T>(promises);
}

template<typename F>
auto promise_resolve(F&& value) {
    return PromiseAll<F>::resolve(std::forward<F>(value));
}

template<typename E>
auto promise_reject(E&& error) {
    return PromiseAll<E>::reject(std::forward<E>(error));
}

}