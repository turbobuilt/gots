#include "libtorch_wrapper.h"
#include <torch/torch.h>
#include <vector>
#include <memory>

// Helper to convert void* back to torch::Tensor
#define TO_TENSOR(ptr) (*static_cast<torch::Tensor*>(ptr))
#define TO_MODULE(ptr) (*static_cast<torch::nn::Linear*>(ptr))

extern "C" {

void* torch_tensor_create_1d(double* data, int64_t size) {
    auto options = torch::TensorOptions().dtype(torch::kFloat64);
    auto tensor = torch::from_blob(data, {size}, options).clone();
    return new torch::Tensor(std::move(tensor));
}

void* torch_tensor_create_2d(double* data, int64_t rows, int64_t cols) {
    auto options = torch::TensorOptions().dtype(torch::kFloat64);
    auto tensor = torch::from_blob(data, {rows, cols}, options).clone();
    return new torch::Tensor(std::move(tensor));
}

void* torch_tensor_zeros(int64_t* shape, int64_t ndim) {
    std::vector<int64_t> shape_vec(shape, shape + ndim);
    auto tensor = torch::zeros(shape_vec, torch::kFloat64);
    return new torch::Tensor(std::move(tensor));
}

void* torch_tensor_ones(int64_t* shape, int64_t ndim) {
    std::vector<int64_t> shape_vec(shape, shape + ndim);
    auto tensor = torch::ones(shape_vec, torch::kFloat64);
    return new torch::Tensor(std::move(tensor));
}

void torch_tensor_destroy(void* tensor) {
    delete static_cast<torch::Tensor*>(tensor);
}

void* torch_tensor_add(void* a, void* b) {
    auto result = TO_TENSOR(a) + TO_TENSOR(b);
    return new torch::Tensor(std::move(result));
}

void* torch_tensor_mul(void* a, void* b) {
    auto result = TO_TENSOR(a) * TO_TENSOR(b);
    return new torch::Tensor(std::move(result));
}

void* torch_tensor_matmul(void* a, void* b) {
    auto result = torch::matmul(TO_TENSOR(a), TO_TENSOR(b));
    return new torch::Tensor(std::move(result));
}

void* torch_tensor_transpose(void* tensor) {
    auto t = TO_TENSOR(tensor);
    auto result = t.transpose(-2, -1);
    return new torch::Tensor(std::move(result));
}

void* torch_linear_create(int64_t in_features, int64_t out_features) {
    auto linear = torch::nn::Linear(in_features, out_features);
    return new torch::nn::Linear(std::move(linear));
}

void* torch_linear_forward(void* layer, void* input) {
    auto& linear = TO_MODULE(layer);
    auto result = linear->forward(TO_TENSOR(input));
    return new torch::Tensor(std::move(result));
}

void* torch_relu(void* tensor) {
    auto result = torch::relu(TO_TENSOR(tensor));
    return new torch::Tensor(std::move(result));
}

void* torch_sigmoid(void* tensor) {
    auto result = torch::sigmoid(TO_TENSOR(tensor));
    return new torch::Tensor(std::move(result));
}

int64_t torch_tensor_ndim(void* tensor) {
    return TO_TENSOR(tensor).dim();
}

void torch_tensor_shape(void* tensor, int64_t* shape_out) {
    auto sizes = TO_TENSOR(tensor).sizes();
    for (size_t i = 0; i < sizes.size(); ++i) {
        shape_out[i] = sizes[i];
    }
}

double* torch_tensor_data_ptr(void* tensor) {
    return TO_TENSOR(tensor).data_ptr<double>();
}

int64_t torch_tensor_numel(void* tensor) {
    return TO_TENSOR(tensor).numel();
}

void torch_tensor_backward(void* tensor) {
    TO_TENSOR(tensor).backward();
}

void* torch_tensor_grad(void* tensor) {
    auto grad = TO_TENSOR(tensor).grad();
    if (grad.defined()) {
        return new torch::Tensor(grad);
    }
    return nullptr;
}

void torch_tensor_requires_grad(void* tensor, bool requires) {
    TO_TENSOR(tensor).requires_grad_(requires);
}

void* torch_optimizer_adam_create(void** parameters, int64_t num_params, double lr) {
    std::vector<torch::Tensor> params;
    for (int64_t i = 0; i < num_params; ++i) {
        params.push_back(TO_TENSOR(parameters[i]));
    }
    auto optimizer = torch::optim::Adam(params, torch::optim::AdamOptions(lr));
    return new torch::optim::Adam(std::move(optimizer));
}

void torch_optimizer_step(void* optimizer) {
    static_cast<torch::optim::Adam*>(optimizer)->step();
}

void torch_optimizer_zero_grad(void* optimizer) {
    static_cast<torch::optim::Adam*>(optimizer)->zero_grad();
}

void torch_optimizer_destroy(void* optimizer) {
    delete static_cast<torch::optim::Adam*>(optimizer);
}

} // extern "C"