// GoTS PyTorch Module
// This module provides a high-level interface to PyTorch via FFI

// FFI declarations for libtorch wrapper
declare function __torch_tensor_create_1d(data: float64*, size: int64): void*;
declare function __torch_tensor_create_2d(data: float64*, rows: int64, cols: int64): void*;
declare function __torch_tensor_zeros(shape: int64*, ndim: int64): void*;
declare function __torch_tensor_ones(shape: int64*, ndim: int64): void*;
declare function __torch_tensor_destroy(tensor: void*): void;
declare function __torch_tensor_add(a: void*, b: void*): void*;
declare function __torch_tensor_mul(a: void*, b: void*): void*;
declare function __torch_tensor_matmul(a: void*, b: void*): void*;
declare function __torch_tensor_transpose(tensor: void*): void*;
declare function __torch_linear_create(in_features: int64, out_features: int64): void*;
declare function __torch_linear_forward(layer: void*, input: void*): void*;
declare function __torch_relu(tensor: void*): void*;
declare function __torch_sigmoid(tensor: void*): void*;
declare function __torch_tensor_ndim(tensor: void*): int64;
declare function __torch_tensor_shape(tensor: void*, shape_out: int64*): void;
declare function __torch_tensor_data_ptr(tensor: void*): float64*;
declare function __torch_tensor_numel(tensor: void*): int64;
declare function __torch_tensor_backward(tensor: void*): void;
declare function __torch_tensor_grad(tensor: void*): void*;
declare function __torch_tensor_requires_grad(tensor: void*, requires: bool): void;
declare function __torch_optimizer_adam_create(parameters: void**, num_params: int64, lr: float64): void*;
declare function __torch_optimizer_step(optimizer: void*): void;
declare function __torch_optimizer_zero_grad(optimizer: void*): void;
declare function __torch_optimizer_destroy(optimizer: void*): void;

// Tensor class wrapper
class Tensor {
    _ptr: void*;
    _shape: [int64];
    _ndim: int64;
    
    // Private constructor - use static factory methods
    constructor(ptr: void*) {
        this._ptr = ptr;
        this._ndim = __torch_tensor_ndim(ptr);
        this._shape = new Array(this._ndim);
        let shape_ptr = new [int64](this._ndim);
        __torch_tensor_shape(ptr, shape_ptr.data);
        for let i = 0; i < this._ndim; ++i {
            this._shape[i] = shape_ptr[i];
        }
    }
    
    // Factory methods
    static zeros(shape: [int64]): Tensor {
        let ptr = __torch_tensor_zeros(shape.data, shape.length);
        return new Tensor(ptr);
    }
    
    static ones(shape: [int64]): Tensor {
        let ptr = __torch_tensor_ones(shape.data, shape.length);
        return new Tensor(ptr);
    }
    
    static from_array(data: [float64], shape?: [int64]): Tensor {
        if !shape {
            // 1D tensor
            let ptr = __torch_tensor_create_1d(data.data, data.length);
            return new Tensor(ptr);
        } else if shape.length == 2 {
            // 2D tensor
            let ptr = __torch_tensor_create_2d(data.data, shape[0], shape[1]);
            return new Tensor(ptr);
        } else {
            throw new Error("Only 1D and 2D tensors supported in from_array");
        }
    }
    
    // Properties
    get shape(): [int64] { return this._shape; }
    get ndim(): int64 { return this._ndim; }
    get size(): int64 { return __torch_tensor_numel(this._ptr); }
    
    // Operations with operator overloading
    operator + (a: Tensor, b: Tensor): Tensor {
        let result_ptr = __torch_tensor_add(a._ptr, b._ptr);
        return new Tensor(result_ptr);
    }
    
    operator * (a: Tensor, b: Tensor): Tensor {
        let result_ptr = __torch_tensor_mul(a._ptr, b._ptr);
        return new Tensor(result_ptr);
    }
    
    // Matrix multiplication
    matmul(other: Tensor): Tensor {
        let result_ptr = __torch_tensor_matmul(this._ptr, other._ptr);
        return new Tensor(result_ptr);
    }
    
    // Transpose
    T(): Tensor {
        let result_ptr = __torch_tensor_transpose(this._ptr);
        return new Tensor(result_ptr);
    }
    
    // Activation functions
    relu(): Tensor {
        let result_ptr = __torch_relu(this._ptr);
        return new Tensor(result_ptr);
    }
    
    sigmoid(): Tensor {
        let result_ptr = __torch_sigmoid(this._ptr);
        return new Tensor(result_ptr);
    }
    
    // Gradient operations
    backward(): void {
        __torch_tensor_backward(this._ptr);
    }
    
    get grad(): Tensor? {
        let grad_ptr = __torch_tensor_grad(this._ptr);
        if grad_ptr {
            return new Tensor(grad_ptr);
        }
        return null;
    }
    
    requires_grad(requires: bool = true): void {
        __torch_tensor_requires_grad(this._ptr, requires);
    }
    
    // Get data as GoTS array
    to_array(): [float64] {
        let size = this.size;
        let data_ptr = __torch_tensor_data_ptr(this._ptr);
        let result = new [float64](size);
        for let i = 0; i < size; ++i {
            result[i] = data_ptr[i];
        }
        return result;
    }
    
    // Destructor
    destroy(): void {
        __torch_tensor_destroy(this._ptr);
    }
}

// Neural Network Layers
class Linear {
    _ptr: void*;
    in_features: int64;
    out_features: int64;
    
    constructor(in_features: int64, out_features: int64) {
        this.in_features = in_features;
        this.out_features = out_features;
        this._ptr = __torch_linear_create(in_features, out_features);
    }
    
    forward(input: Tensor): Tensor {
        let result_ptr = __torch_linear_forward(this._ptr, input._ptr);
        return new Tensor(result_ptr);
    }
    
    // Operator overloading for function call syntax
    operator () (layer: Linear, input: Tensor): Tensor {
        return layer.forward(input);
    }
}

// Optimizer
class AdamOptimizer {
    _ptr: void*;
    
    constructor(parameters: [Tensor], lr: float64 = 0.001) {
        let param_ptrs = new [void*](parameters.length);
        for let i = 0; i < parameters.length; ++i {
            param_ptrs[i] = parameters[i]._ptr;
        }
        this._ptr = __torch_optimizer_adam_create(param_ptrs.data, parameters.length, lr);
    }
    
    step(): void {
        __torch_optimizer_step(this._ptr);
    }
    
    zero_grad(): void {
        __torch_optimizer_zero_grad(this._ptr);
    }
    
    destroy(): void {
        __torch_optimizer_destroy(this._ptr);
    }
}

// Convenience namespace
namespace torch {
    export Tensor;
    export Linear;
    export AdamOptimizer;
    
    export function zeros(shape: [int64]): Tensor {
        return Tensor.zeros(shape);
    }
    
    export function ones(shape: [int64]): Tensor {
        return Tensor.ones(shape);
    }
    
    export function tensor(data: [float64], shape?: [int64]): Tensor {
        return Tensor.from_array(data, shape);
    }
}

// Export for module use
export torch;