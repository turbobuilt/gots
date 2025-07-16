#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Tensor creation and manipulation
void* torch_tensor_create_1d(double* data, int64_t size);
void* torch_tensor_create_2d(double* data, int64_t rows, int64_t cols);
void* torch_tensor_zeros(int64_t* shape, int64_t ndim);
void* torch_tensor_ones(int64_t* shape, int64_t ndim);
void torch_tensor_destroy(void* tensor);

// Basic operations
void* torch_tensor_add(void* a, void* b);
void* torch_tensor_mul(void* a, void* b);
void* torch_tensor_matmul(void* a, void* b);
void* torch_tensor_transpose(void* tensor);

// Neural network operations
void* torch_linear_create(int64_t in_features, int64_t out_features);
void* torch_linear_forward(void* layer, void* input);
void* torch_relu(void* tensor);
void* torch_sigmoid(void* tensor);

// Tensor properties
int64_t torch_tensor_ndim(void* tensor);
void torch_tensor_shape(void* tensor, int64_t* shape_out);
double* torch_tensor_data_ptr(void* tensor);
int64_t torch_tensor_numel(void* tensor);

// Gradient operations
void torch_tensor_backward(void* tensor);
void* torch_tensor_grad(void* tensor);
void torch_tensor_requires_grad(void* tensor, bool requires);

// Optimizer
void* torch_optimizer_adam_create(void** parameters, int64_t num_params, double lr);
void torch_optimizer_step(void* optimizer);
void torch_optimizer_zero_grad(void* optimizer);
void torch_optimizer_destroy(void* optimizer);

#ifdef __cplusplus
}
#endif