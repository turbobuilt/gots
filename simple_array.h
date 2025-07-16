#pragma once

#include <vector>
#include <memory>
#include <initializer_list>
#include <stdexcept>
#include <algorithm>
#include <random>
#include <cmath>
#include <sstream>

namespace gots {

// Simple unified Array class - works for all dimensions
class Array {
private:
    std::vector<double> data_;
    std::vector<size_t> shape_;
    
    // Calculate flat index from multi-dimensional indices
    size_t get_flat_index(const std::vector<size_t>& indices) const {
        if (indices.size() != shape_.size()) {
            throw std::runtime_error("Dimension mismatch");
        }
        
        size_t flat_index = 0;
        size_t stride = 1;
        
        for (int i = static_cast<int>(shape_.size()) - 1; i >= 0; --i) {
            if (indices[i] >= shape_[i]) {
                throw std::runtime_error("Index out of bounds");
            }
            flat_index += indices[i] * stride;
            stride *= shape_[i];
        }
        
        return flat_index;
    }
    
public:
    // Constructors
    Array() : shape_({0}) {}
    
    // Create from initializer list (1D)
    Array(std::initializer_list<double> values) : data_(values), shape_({data_.size()}) {}
    
    // Create with specific shape (filled with zeros)
    explicit Array(const std::vector<size_t>& shape) : shape_(shape) {
        size_t total_size = 1;
        for (size_t dim : shape) {
            total_size *= dim;
        }
        data_.resize(total_size, 0.0);
    }
    
    // Create with shape and data
    Array(const std::vector<size_t>& shape, const std::vector<double>& values) 
        : data_(values), shape_(shape) {
        size_t expected_size = 1;
        for (size_t dim : shape) {
            expected_size *= dim;
        }
        if (data_.size() != expected_size) {
            throw std::runtime_error("Data size doesn't match shape");
        }
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
    
    bool is_1d() const {
        return shape_.size() == 1;
    }
    
    // 1D Array operations (only work for 1D arrays)
    void push(double value) {
        if (!is_1d()) {
            throw std::runtime_error("push() only works on 1D arrays");
        }
        data_.push_back(value);
        shape_[0] = data_.size();
    }
    
    double pop() {
        if (!is_1d() || data_.empty()) {
            throw std::runtime_error("pop() only works on non-empty 1D arrays");
        }
        double value = data_.back();
        data_.pop_back();
        shape_[0] = data_.size();
        return value;
    }
    
    // Element access
    double& operator[](size_t index) {
        if (!is_1d()) {
            throw std::runtime_error("operator[] only works on 1D arrays, use at() for multi-dimensional");
        }
        if (index >= shape_[0]) {
            throw std::runtime_error("Index out of bounds");
        }
        return data_[index];
    }
    
    const double& operator[](size_t index) const {
        if (!is_1d()) {
            throw std::runtime_error("operator[] only works on 1D arrays, use at() for multi-dimensional");
        }
        if (index >= shape_[0]) {
            throw std::runtime_error("Index out of bounds");
        }
        return data_[index];
    }
    
    // Multi-dimensional element access
    double& at(const std::vector<size_t>& indices) {
        return data_[get_flat_index(indices)];
    }
    
    const double& at(const std::vector<size_t>& indices) const {
        return data_[get_flat_index(indices)];
    }
    
    // Slice operations - returns new Array
    Array slice(int64_t start, int64_t end = -1, int64_t step = 1) const {
        if (!is_1d()) {
            throw std::runtime_error("Simple slice only works on 1D arrays");
        }
        
        size_t len = shape_[0];
        
        // Handle negative indices
        if (start < 0) start += static_cast<int64_t>(len);
        if (end < 0) end += static_cast<int64_t>(len);
        
        // Clamp to valid range
        start = std::max(0L, std::min(start, static_cast<int64_t>(len)));
        end = std::max(0L, std::min(end, static_cast<int64_t>(len)));
        
        if (step <= 0) {
            throw std::runtime_error("Step must be positive");
        }
        
        std::vector<double> result_data;
        for (int64_t i = start; i < end; i += step) {
            result_data.push_back(data_[i]);
        }
        
        return Array({result_data.size()}, result_data);
    }
    
    // Get all elements (equivalent to [:])
    Array slice_all() const {
        return Array(shape_, data_);
    }
    
    // Arithmetic operations
    Array operator+(const Array& other) const {
        if (shape_ != other.shape_) {
            throw std::runtime_error("Shape mismatch for addition");
        }
        
        std::vector<double> result_data(data_.size());
        for (size_t i = 0; i < data_.size(); ++i) {
            result_data[i] = data_[i] + other.data_[i];
        }
        
        return Array(shape_, result_data);
    }
    
    Array operator-(const Array& other) const {
        if (shape_ != other.shape_) {
            throw std::runtime_error("Shape mismatch for subtraction");
        }
        
        std::vector<double> result_data(data_.size());
        for (size_t i = 0; i < data_.size(); ++i) {
            result_data[i] = data_[i] - other.data_[i];
        }
        
        return Array(shape_, result_data);
    }
    
    Array operator*(const Array& other) const {
        if (shape_ != other.shape_) {
            throw std::runtime_error("Shape mismatch for multiplication");
        }
        
        std::vector<double> result_data(data_.size());
        for (size_t i = 0; i < data_.size(); ++i) {
            result_data[i] = data_[i] * other.data_[i];
        }
        
        return Array(shape_, result_data);
    }
    
    // Scalar operations
    Array operator+(double scalar) const {
        std::vector<double> result_data(data_.size());
        for (size_t i = 0; i < data_.size(); ++i) {
            result_data[i] = data_[i] + scalar;
        }
        return Array(shape_, result_data);
    }
    
    Array operator*(double scalar) const {
        std::vector<double> result_data(data_.size());
        for (size_t i = 0; i < data_.size(); ++i) {
            result_data[i] = data_[i] * scalar;
        }
        return Array(shape_, result_data);
    }
    
    // Statistical operations
    double sum() const {
        double result = 0.0;
        for (double value : data_) {
            result += value;
        }
        return result;
    }
    
    double mean() const {
        if (data_.empty()) {
            throw std::runtime_error("Cannot compute mean of empty array");
        }
        return sum() / static_cast<double>(data_.size());
    }
    
    double max() const {
        if (data_.empty()) {
            throw std::runtime_error("Cannot compute max of empty array");
        }
        return *std::max_element(data_.begin(), data_.end());
    }
    
    double min() const {
        if (data_.empty()) {
            throw std::runtime_error("Cannot compute min of empty array");
        }
        return *std::min_element(data_.begin(), data_.end());
    }
    
    // Shape manipulation
    Array reshape(const std::vector<size_t>& new_shape) const {
        size_t new_size = 1;
        for (size_t dim : new_shape) {
            new_size *= dim;
        }
        
        if (new_size != data_.size()) {
            throw std::runtime_error("Cannot reshape: size mismatch");
        }
        
        return Array(new_shape, data_);
    }
    
    Array flatten() const {
        return Array({data_.size()}, data_);
    }
    
    // Static factory methods (PyTorch-style)
    static Array zeros(const std::vector<size_t>& shape) {
        return Array(shape);  // Constructor already fills with zeros
    }
    
    static Array ones(const std::vector<size_t>& shape) {
        size_t total_size = 1;
        for (size_t dim : shape) {
            total_size *= dim;
        }
        std::vector<double> data(total_size, 1.0);
        return Array(shape, data);
    }
    
    static Array full(const std::vector<size_t>& shape, double value) {
        size_t total_size = 1;
        for (size_t dim : shape) {
            total_size *= dim;
        }
        std::vector<double> data(total_size, value);
        return Array(shape, data);
    }
    
    static Array arange(double start, double stop, double step = 1.0) {
        if (step == 0.0) {
            throw std::runtime_error("Step cannot be zero");
        }
        
        std::vector<double> values;
        if (step > 0.0) {
            for (double val = start; val < stop; val += step) {
                values.push_back(val);
            }
        } else {
            for (double val = start; val > stop; val += step) {
                values.push_back(val);
            }
        }
        
        return Array({values.size()}, values);
    }
    
    static Array linspace(double start, double stop, size_t num = 50) {
        if (num == 0) {
            return Array({0});
        }
        if (num == 1) {
            return Array({start});
        }
        
        std::vector<double> values;
        values.reserve(num);
        
        double step = (stop - start) / static_cast<double>(num - 1);
        for (size_t i = 0; i < num; ++i) {
            values.push_back(start + static_cast<double>(i) * step);
        }
        
        return Array({num}, values);
    }
    
    static Array random(const std::vector<size_t>& shape, double min_val = 0.0, double max_val = 1.0) {
        size_t total_size = 1;
        for (size_t dim : shape) {
            total_size *= dim;
        }
        
        std::vector<double> data;
        data.reserve(total_size);
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<double> dis(min_val, max_val);
        
        for (size_t i = 0; i < total_size; ++i) {
            data.push_back(dis(gen));
        }
        
        return Array(shape, data);
    }
    
    // String representation
    std::string toString() const {
        std::ostringstream oss;
        oss << "Array(shape=[";
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

}