#pragma once

#include <vector>
#include <memory>
#include <initializer_list>
#include <cstring>
#include <stdexcept>
#include <functional>
#include <algorithm>
#include <random>
#include <cmath>
#include <sstream>

namespace gots {

// Slice type for PyTorch-style slicing
struct Slice {
    int64_t start;
    int64_t end;
    int64_t step;
    bool start_specified;
    bool end_specified;
    bool step_specified;
    
    Slice() : start(0), end(-1), step(1), start_specified(false), end_specified(false), step_specified(false) {}
    
    Slice(int64_t start_val) 
        : start(start_val), end(-1), step(1), start_specified(true), end_specified(false), step_specified(false) {}
    
    Slice(int64_t start_val, int64_t end_val) 
        : start(start_val), end(end_val), step(1), start_specified(true), end_specified(true), step_specified(false) {}
    
    Slice(int64_t start_val, int64_t end_val, int64_t step_val) 
        : start(start_val), end(end_val), step(step_val), start_specified(true), end_specified(true), step_specified(true) {}
    
    // Resolve slice against actual dimension size
    void resolve(size_t dim_size) {
        if (!start_specified) start = 0;
        if (!end_specified) end = static_cast<int64_t>(dim_size);
        if (!step_specified) step = 1;
        
        // Handle negative indices
        if (start < 0) start += static_cast<int64_t>(dim_size);
        if (end < 0) end += static_cast<int64_t>(dim_size);
        
        // Clamp to valid range
        start = std::max(0L, std::min(start, static_cast<int64_t>(dim_size)));
        end = std::max(0L, std::min(end, static_cast<int64_t>(dim_size)));
    }
};

// Built-in GotsArray class for maximum performance
template<typename T = double>
class GotsArray {
private:
    std::vector<T> data_;
    std::vector<size_t> shape_;
    std::vector<size_t> strides_;
    
    void calculate_strides() {
        strides_.resize(shape_.size());
        if (shape_.empty()) return;
        
        size_t stride = 1;
        for (int i = static_cast<int>(shape_.size()) - 1; i >= 0; --i) {
            strides_[i] = stride;
            stride *= shape_[i];
        }
    }
    
    size_t get_flat_index(const std::vector<size_t>& indices) const {
        if (indices.size() != shape_.size()) {
            throw std::runtime_error("Dimension mismatch in array access");
        }
        
        size_t flat_index = 0;
        for (size_t i = 0; i < indices.size(); ++i) {
            if (indices[i] >= shape_[i]) {
                throw std::runtime_error("Index out of bounds");
            }
            flat_index += indices[i] * strides_[i];
        }
        return flat_index;
    }
    
public:
    // Constructors
    GotsArray() = default;
    
    // Create array with specified shape
    explicit GotsArray(const std::vector<size_t>& shape) : shape_(shape) {
        size_t total_size = 1;
        for (size_t dim : shape) {
            total_size *= dim;
        }
        data_.resize(total_size);
        calculate_strides();
    }
    
    // Create array with shape and data
    GotsArray(const std::vector<size_t>& shape, const std::vector<T>& values) 
        : shape_(shape), data_(values) {
        size_t expected_size = 1;
        for (size_t dim : shape) {
            expected_size *= dim;
        }
        if (data_.size() != expected_size) {
            throw std::runtime_error("Data size doesn't match shape");
        }
        calculate_strides();
    }
    
    // Create 1D array from initializer list
    GotsArray(std::initializer_list<T> values) : data_(values) {
        shape_ = {data_.size()};
        calculate_strides();
    }
    
    // Properties
    const std::vector<size_t>& shape() const { return shape_; }
    
    size_t length() const {
        return shape_.empty() ? 0 : shape_[0];
    }
    
    size_t size() const {
        return data_.size();
    }
    
    size_t ndim() const {
        return shape_.size();
    }
    
    bool empty() const {
        return data_.empty();
    }
    
    // Element access
    T& operator[](size_t index) {
        if (shape_.size() != 1) {
            throw std::runtime_error("Multi-dimensional array requires multiple indices");
        }
        if (index >= shape_[0]) {
            throw std::runtime_error("Index out of bounds");
        }
        return data_[index];
    }
    
    const T& operator[](size_t index) const {
        if (shape_.size() != 1) {
            throw std::runtime_error("Multi-dimensional array requires multiple indices");
        }
        if (index >= shape_[0]) {
            throw std::runtime_error("Index out of bounds");
        }
        return data_[index];
    }
    
    T& at(const std::vector<size_t>& indices) {
        return data_[get_flat_index(indices)];
    }
    
    const T& at(const std::vector<size_t>& indices) const {
        return data_[get_flat_index(indices)];
    }
    
    // Array operations
    void push(const T& value) {
        if (shape_.size() != 1) {
            throw std::runtime_error("push() only works on 1D arrays");
        }
        data_.push_back(value);
        shape_[0] = data_.size();
    }
    
    T pop() {
        if (shape_.size() != 1 || data_.empty()) {
            throw std::runtime_error("pop() only works on non-empty 1D arrays");
        }
        T value = data_.back();
        data_.pop_back();
        shape_[0] = data_.size();
        return value;
    }
    
    // Slice operator implementation
    GotsArray<T> slice(const std::vector<Slice>& slices) const {
        if (slices.size() > shape_.size()) {
            throw std::runtime_error("Too many slice dimensions");
        }
        
        std::vector<Slice> resolved_slices = slices;
        resolved_slices.resize(shape_.size());
        
        // Resolve slices and calculate new shape
        std::vector<size_t> new_shape;
        for (size_t i = 0; i < shape_.size(); ++i) {
            resolved_slices[i].resolve(shape_[i]);
            
            if (resolved_slices[i].step == 0) {
                throw std::runtime_error("Slice step cannot be zero");
            }
            
            int64_t start = resolved_slices[i].start;
            int64_t end = resolved_slices[i].end;
            int64_t step = resolved_slices[i].step;
            
            if (step > 0) {
                if (start >= end) {
                    new_shape.push_back(0);
                } else {
                    new_shape.push_back((end - start + step - 1) / step);
                }
            } else {
                if (start <= end) {
                    new_shape.push_back(0);
                } else {
                    new_shape.push_back((start - end - step - 1) / (-step));
                }
            }
        }
        
        GotsArray<T> result(new_shape);
        
        // Copy sliced data
        std::function<void(std::vector<size_t>&, size_t)> copy_slice = 
            [&](std::vector<size_t>& dst_indices, size_t dim) {
                if (dim == shape_.size()) {
                    // Convert destination indices to source indices
                    std::vector<size_t> src_indices(shape_.size());
                    for (size_t i = 0; i < shape_.size(); ++i) {
                        if (resolved_slices[i].step > 0) {
                            src_indices[i] = resolved_slices[i].start + dst_indices[i] * resolved_slices[i].step;
                        } else {
                            src_indices[i] = resolved_slices[i].start + dst_indices[i] * resolved_slices[i].step;
                        }
                    }
                    result.at(dst_indices) = this->at(src_indices);
                    return;
                }
                
                for (size_t i = 0; i < new_shape[dim]; ++i) {
                    dst_indices[dim] = i;
                    copy_slice(dst_indices, dim + 1);
                }
            };
        
        if (!new_shape.empty()) {
            std::vector<size_t> dst_indices(new_shape.size(), 0);
            copy_slice(dst_indices, 0);
        }
        
        return result;
    }
    
    // Convenience slice methods
    GotsArray<T> slice(int64_t start) const {
        return slice({Slice(start)});
    }
    
    GotsArray<T> slice(int64_t start, int64_t end) const {
        return slice({Slice(start, end)});
    }
    
    GotsArray<T> slice(int64_t start, int64_t end, int64_t step) const {
        return slice({Slice(start, end, step)});
    }
    
    // Arithmetic operations
    GotsArray<T> operator+(const GotsArray<T>& other) const {
        if (shape_ != other.shape_) {
            throw std::runtime_error("Shape mismatch for addition");
        }
        
        GotsArray<T> result(shape_);
        for (size_t i = 0; i < data_.size(); ++i) {
            result.data_[i] = data_[i] + other.data_[i];
        }
        return result;
    }
    
    GotsArray<T> operator-(const GotsArray<T>& other) const {
        if (shape_ != other.shape_) {
            throw std::runtime_error("Shape mismatch for subtraction");
        }
        
        GotsArray<T> result(shape_);
        for (size_t i = 0; i < data_.size(); ++i) {
            result.data_[i] = data_[i] - other.data_[i];
        }
        return result;
    }
    
    GotsArray<T> operator*(const GotsArray<T>& other) const {
        if (shape_ != other.shape_) {
            throw std::runtime_error("Shape mismatch for multiplication");
        }
        
        GotsArray<T> result(shape_);
        for (size_t i = 0; i < data_.size(); ++i) {
            result.data_[i] = data_[i] * other.data_[i];
        }
        return result;
    }
    
    GotsArray<T> operator/(const GotsArray<T>& other) const {
        if (shape_ != other.shape_) {
            throw std::runtime_error("Shape mismatch for division");
        }
        
        GotsArray<T> result(shape_);
        for (size_t i = 0; i < data_.size(); ++i) {
            if (other.data_[i] == T(0)) {
                throw std::runtime_error("Division by zero");
            }
            result.data_[i] = data_[i] / other.data_[i];
        }
        return result;
    }
    
    // Scalar operations
    GotsArray<T> operator+(const T& scalar) const {
        GotsArray<T> result(shape_);
        for (size_t i = 0; i < data_.size(); ++i) {
            result.data_[i] = data_[i] + scalar;
        }
        return result;
    }
    
    GotsArray<T> operator-(const T& scalar) const {
        GotsArray<T> result(shape_);
        for (size_t i = 0; i < data_.size(); ++i) {
            result.data_[i] = data_[i] - scalar;
        }
        return result;
    }
    
    GotsArray<T> operator*(const T& scalar) const {
        GotsArray<T> result(shape_);
        for (size_t i = 0; i < data_.size(); ++i) {
            result.data_[i] = data_[i] * scalar;
        }
        return result;
    }
    
    GotsArray<T> operator/(const T& scalar) const {
        if (scalar == T(0)) {
            throw std::runtime_error("Division by zero");
        }
        GotsArray<T> result(shape_);
        for (size_t i = 0; i < data_.size(); ++i) {
            result.data_[i] = data_[i] / scalar;
        }
        return result;
    }
    
    // Dot product (matrix multiplication)
    GotsArray<T> dot(const GotsArray<T>& other) const {
        // Handle 1D dot product
        if (shape_.size() == 1 && other.shape_.size() == 1) {
            if (shape_[0] != other.shape_[0]) {
                throw std::runtime_error("Incompatible shapes for dot product");
            }
            
            GotsArray<T> result({1});
            T sum = T(0);
            for (size_t i = 0; i < data_.size(); ++i) {
                sum += data_[i] * other.data_[i];
            }
            result.data_[0] = sum;
            return result;
        }
        
        // Handle matrix multiplication
        if (shape_.size() == 2 && other.shape_.size() == 2) {
            if (shape_[1] != other.shape_[0]) {
                throw std::runtime_error("Incompatible shapes for matrix multiplication");
            }
            
            std::vector<size_t> result_shape = {shape_[0], other.shape_[1]};
            GotsArray<T> result(result_shape);
            
            for (size_t i = 0; i < shape_[0]; ++i) {
                for (size_t j = 0; j < other.shape_[1]; ++j) {
                    T sum = T(0);
                    for (size_t k = 0; k < shape_[1]; ++k) {
                        sum += this->at({i, k}) * other.at({k, j});
                    }
                    result.at({i, j}) = sum;
                }
            }
            
            return result;
        }
        
        throw std::runtime_error("Unsupported shapes for dot product");
    }
    
    // Statistical operations
    T sum() const {
        T result = T(0);
        for (const T& value : data_) {
            result += value;
        }
        return result;
    }
    
    T mean() const {
        if (data_.empty()) {
            throw std::runtime_error("Cannot compute mean of empty array");
        }
        return sum() / static_cast<T>(data_.size());
    }
    
    T max() const {
        if (data_.empty()) {
            throw std::runtime_error("Cannot compute max of empty array");
        }
        return *std::max_element(data_.begin(), data_.end());
    }
    
    T min() const {
        if (data_.empty()) {
            throw std::runtime_error("Cannot compute min of empty array");
        }
        return *std::min_element(data_.begin(), data_.end());
    }
    
    T std() const {
        if (data_.empty()) {
            throw std::runtime_error("Cannot compute std of empty array");
        }
        T m = mean();
        T sum_sq_diff = T(0);
        for (const T& value : data_) {
            T diff = value - m;
            sum_sq_diff += diff * diff;
        }
        return std::sqrt(sum_sq_diff / static_cast<T>(data_.size()));
    }
    
    // Shape manipulation
    GotsArray<T> reshape(const std::vector<size_t>& new_shape) const {
        size_t new_size = 1;
        for (size_t dim : new_shape) {
            new_size *= dim;
        }
        
        if (new_size != data_.size()) {
            throw std::runtime_error("Cannot reshape: size mismatch");
        }
        
        return GotsArray<T>(new_shape, data_);
    }
    
    GotsArray<T> transpose() const {
        if (shape_.size() != 2) {
            throw std::runtime_error("Transpose only works on 2D arrays");
        }
        
        std::vector<size_t> new_shape = {shape_[1], shape_[0]};
        GotsArray<T> result(new_shape);
        
        for (size_t i = 0; i < shape_[0]; ++i) {
            for (size_t j = 0; j < shape_[1]; ++j) {
                result.at({j, i}) = this->at({i, j});
            }
        }
        
        return result;
    }
    
    GotsArray<T> flatten() const {
        return GotsArray<T>({data_.size()}, data_);
    }
    
    // Static factory methods (PyTorch-style)
    static GotsArray<T> zeros(const std::vector<size_t>& shape) {
        GotsArray<T> result(shape);
        std::fill(result.data_.begin(), result.data_.end(), T(0));
        return result;
    }
    
    static GotsArray<T> ones(const std::vector<size_t>& shape) {
        GotsArray<T> result(shape);
        std::fill(result.data_.begin(), result.data_.end(), T(1));
        return result;
    }
    
    static GotsArray<T> full(const std::vector<size_t>& shape, const T& value) {
        GotsArray<T> result(shape);
        std::fill(result.data_.begin(), result.data_.end(), value);
        return result;
    }
    
    static GotsArray<T> eye(size_t n) {
        GotsArray<T> result({n, n});
        for (size_t i = 0; i < n; ++i) {
            result.at({i, i}) = T(1);
        }
        return result;
    }
    
    static GotsArray<T> arange(T start, T stop, T step = T(1)) {
        if (step == T(0)) {
            throw std::runtime_error("Step cannot be zero");
        }
        
        std::vector<T> values;
        if (step > T(0)) {
            for (T val = start; val < stop; val += step) {
                values.push_back(val);
            }
        } else {
            for (T val = start; val > stop; val += step) {
                values.push_back(val);
            }
        }
        
        return GotsArray<T>({values.size()}, values);
    }
    
    static GotsArray<T> linspace(T start, T stop, size_t num = 50) {
        if (num == 0) {
            return GotsArray<T>({0});
        }
        if (num == 1) {
            return GotsArray<T>({1}, {start});
        }
        
        std::vector<T> values;
        values.reserve(num);
        
        T step = (stop - start) / static_cast<T>(num - 1);
        for (size_t i = 0; i < num; ++i) {
            values.push_back(start + static_cast<T>(i) * step);
        }
        
        return GotsArray<T>({num}, values);
    }
    
    static GotsArray<T> logspace(T start, T stop, size_t num = 50, T base = T(10)) {
        GotsArray<T> linear = linspace(start, stop, num);
        GotsArray<T> result(linear.shape_);
        
        for (size_t i = 0; i < linear.data_.size(); ++i) {
            result.data_[i] = std::pow(base, linear.data_[i]);
        }
        
        return result;
    }
    
    static GotsArray<T> random(const std::vector<size_t>& shape, T min_val = T(0), T max_val = T(1)) {
        GotsArray<T> result(shape);
        
        std::random_device rd;
        std::mt19937 gen(rd());
        
        if constexpr (std::is_integral_v<T>) {
            std::uniform_int_distribution<T> dis(min_val, max_val);
            for (auto& val : result.data_) {
                val = dis(gen);
            }
        } else {
            std::uniform_real_distribution<T> dis(min_val, max_val);
            for (auto& val : result.data_) {
                val = dis(gen);
            }
        }
        
        return result;
    }
    
    static GotsArray<T> randn(const std::vector<size_t>& shape, T mean = T(0), T stddev = T(1)) {
        GotsArray<T> result(shape);
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::normal_distribution<T> dis(mean, stddev);
        
        for (auto& val : result.data_) {
            val = dis(gen);
        }
        
        return result;
    }
    
    // String representation
    std::string toString() const {
        std::ostringstream oss;
        oss << "GotsArray(shape=[";
        for (size_t i = 0; i < shape_.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << shape_[i];
        }
        oss << "], data=[";
        
        size_t max_display = std::min(size_t(10), data_.size());
        for (size_t i = 0; i < max_display; ++i) {
            if (i > 0) oss << ", ";
            oss << data_[i];
        }
        if (data_.size() > max_display) {
            oss << ", ...";
        }
        oss << "])";
        return oss.str();
    }
};

// Type aliases for convenience
using array = GotsArray<double>;
using array_f32 = GotsArray<float>;
using array_i32 = GotsArray<int32_t>;
using array_i64 = GotsArray<int64_t>;

}