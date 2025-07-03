#pragma once

#include <vector>
#include <memory>
#include <initializer_list>
#include <cstring>
#include <stdexcept>
#include <functional>
#include <algorithm>

namespace gots {

template<typename T = double>
class Tensor {
private:
    std::vector<T> data;
    std::vector<size_t> shape_;
    std::vector<size_t> strides;
    
    void calculate_strides() {
        strides.resize(shape_.size());
        size_t stride = 1;
        for (int i = shape_.size() - 1; i >= 0; --i) {
            strides[i] = stride;
            stride *= shape_[i];
        }
    }
    
    size_t get_index(const std::vector<size_t>& indices) const {
        if (indices.size() != shape_.size()) {
            throw std::runtime_error("Dimension mismatch");
        }
        
        size_t index = 0;
        for (size_t i = 0; i < indices.size(); ++i) {
            if (indices[i] >= shape_[i]) {
                throw std::runtime_error("Index out of bounds");
            }
            index += indices[i] * strides[i];
        }
        return index;
    }
    
public:
    Tensor() = default;
    
    Tensor(const std::vector<size_t>& shape) : shape_(shape) {
        size_t total_size = 1;
        for (size_t dim : shape) {
            total_size *= dim;
        }
        data.resize(total_size);
        calculate_strides();
    }
    
    Tensor(const std::vector<size_t>& shape, const std::vector<T>& values) 
        : shape_(shape), data(values) {
        size_t expected_size = 1;
        for (size_t dim : shape) {
            expected_size *= dim;
        }
        if (data.size() != expected_size) {
            throw std::runtime_error("Data size doesn't match shape");
        }
        calculate_strides();
    }
    
    Tensor(std::initializer_list<T> values) {
        data = values;
        shape_ = {data.size()};
        calculate_strides();
    }
    
    const std::vector<size_t>& shape() const {
        return shape_;
    }
    
    size_t size() const {
        return data.size();
    }
    
    size_t ndim() const {
        return shape_.size();
    }
    
    T& operator[](size_t index) {
        if (shape_.size() != 1) {
            throw std::runtime_error("Multi-dimensional tensor requires multiple indices");
        }
        return data[index];
    }
    
    const T& operator[](size_t index) const {
        if (shape_.size() != 1) {
            throw std::runtime_error("Multi-dimensional tensor requires multiple indices");
        }
        return data[index];
    }
    
    T& at(const std::vector<size_t>& indices) {
        return data[get_index(indices)];
    }
    
    const T& at(const std::vector<size_t>& indices) const {
        return data[get_index(indices)];
    }
    
    void push(const T& value) {
        if (shape_.size() != 1) {
            throw std::runtime_error("push() only works on 1D tensors");
        }
        data.push_back(value);
        shape_[0] = data.size();
    }
    
    T pop() {
        if (shape_.size() != 1 || data.empty()) {
            throw std::runtime_error("pop() only works on non-empty 1D tensors");
        }
        T value = data.back();
        data.pop_back();
        shape_[0] = data.size();
        return value;
    }
    
    Tensor<T> reshape(const std::vector<size_t>& new_shape) const {
        size_t new_size = 1;
        for (size_t dim : new_shape) {
            new_size *= dim;
        }
        
        if (new_size != data.size()) {
            throw std::runtime_error("Cannot reshape: size mismatch");
        }
        
        Tensor<T> result(new_shape, data);
        return result;
    }
    
    Tensor<T> slice(const std::vector<std::pair<size_t, size_t>>& ranges) const {
        if (ranges.size() != shape_.size()) {
            throw std::runtime_error("Slice ranges must match tensor dimensions");
        }
        
        std::vector<size_t> new_shape;
        for (size_t i = 0; i < ranges.size(); ++i) {
            size_t start = ranges[i].first;
            size_t end = ranges[i].second;
            if (start >= shape_[i] || end > shape_[i] || start >= end) {
                throw std::runtime_error("Invalid slice range");
            }
            new_shape.push_back(end - start);
        }
        
        Tensor<T> result(new_shape);
        
        std::function<void(std::vector<size_t>&, size_t)> copy_slice = 
            [&](std::vector<size_t>& indices, size_t dim) {
                if (dim == shape_.size()) {
                    std::vector<size_t> src_indices = indices;
                    std::vector<size_t> dst_indices;
                    for (size_t i = 0; i < indices.size(); ++i) {
                        src_indices[i] += ranges[i].first;
                        dst_indices.push_back(indices[i]);
                    }
                    result.at(dst_indices) = this->at(src_indices);
                    return;
                }
                
                for (size_t i = 0; i < new_shape[dim]; ++i) {
                    indices[dim] = i;
                    copy_slice(indices, dim + 1);
                }
            };
        
        std::vector<size_t> indices(shape_.size(), 0);
        copy_slice(indices, 0);
        
        return result;
    }
    
    Tensor<T> operator+(const Tensor<T>& other) const {
        if (shape_ != other.shape_) {
            throw std::runtime_error("Shape mismatch for addition");
        }
        
        Tensor<T> result(shape_);
        for (size_t i = 0; i < data.size(); ++i) {
            result.data[i] = data[i] + other.data[i];
        }
        return result;
    }
    
    Tensor<T> operator-(const Tensor<T>& other) const {
        if (shape_ != other.shape_) {
            throw std::runtime_error("Shape mismatch for subtraction");
        }
        
        Tensor<T> result(shape_);
        for (size_t i = 0; i < data.size(); ++i) {
            result.data[i] = data[i] - other.data[i];
        }
        return result;
    }
    
    Tensor<T> operator*(const Tensor<T>& other) const {
        if (shape_ != other.shape_) {
            throw std::runtime_error("Shape mismatch for multiplication");
        }
        
        Tensor<T> result(shape_);
        for (size_t i = 0; i < data.size(); ++i) {
            result.data[i] = data[i] * other.data[i];
        }
        return result;
    }
    
    Tensor<T> operator/(const Tensor<T>& other) const {
        if (shape_ != other.shape_) {
            throw std::runtime_error("Shape mismatch for division");
        }
        
        Tensor<T> result(shape_);
        for (size_t i = 0; i < data.size(); ++i) {
            if (other.data[i] == 0) {
                throw std::runtime_error("Division by zero");
            }
            result.data[i] = data[i] / other.data[i];
        }
        return result;
    }
    
    static Tensor<T> zeros(const std::vector<size_t>& shape) {
        Tensor<T> result(shape);
        std::fill(result.data.begin(), result.data.end(), T(0));
        return result;
    }
    
    static Tensor<T> ones(const std::vector<size_t>& shape) {
        Tensor<T> result(shape);
        std::fill(result.data.begin(), result.data.end(), T(1));
        return result;
    }
    
    static Tensor<T> full(const std::vector<size_t>& shape, const T& value) {
        Tensor<T> result(shape);
        std::fill(result.data.begin(), result.data.end(), value);
        return result;
    }
    
    Tensor<T> transpose() const {
        if (shape_.size() != 2) {
            throw std::runtime_error("Transpose only works on 2D tensors");
        }
        
        std::vector<size_t> new_shape = {shape_[1], shape_[0]};
        Tensor<T> result(new_shape);
        
        for (size_t i = 0; i < shape_[0]; ++i) {
            for (size_t j = 0; j < shape_[1]; ++j) {
                result.at({j, i}) = this->at({i, j});
            }
        }
        
        return result;
    }
    
    Tensor<T> matmul(const Tensor<T>& other) const {
        if (shape_.size() != 2 || other.shape_.size() != 2) {
            throw std::runtime_error("Matrix multiplication requires 2D tensors");
        }
        
        if (shape_[1] != other.shape_[0]) {
            throw std::runtime_error("Incompatible shapes for matrix multiplication");
        }
        
        std::vector<size_t> result_shape = {shape_[0], other.shape_[1]};
        Tensor<T> result(result_shape);
        
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
    
    T sum() const {
        T result = T(0);
        for (const T& value : data) {
            result += value;
        }
        return result;
    }
    
    T mean() const {
        if (data.empty()) {
            throw std::runtime_error("Cannot compute mean of empty tensor");
        }
        return sum() / static_cast<T>(data.size());
    }
    
    T max() const {
        if (data.empty()) {
            throw std::runtime_error("Cannot compute max of empty tensor");
        }
        return *std::max_element(data.begin(), data.end());
    }
    
    T min() const {
        if (data.empty()) {
            throw std::runtime_error("Cannot compute min of empty tensor");
        }
        return *std::min_element(data.begin(), data.end());
    }
};

using tensor = Tensor<double>;

}