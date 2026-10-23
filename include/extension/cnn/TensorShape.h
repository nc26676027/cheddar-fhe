#pragma once

#include <cmath>
#include <complex>
#include <vector>

#include "common/Assert.h"

namespace cheddar {
namespace cnn {

using Complex = std::complex<double>;

/**
 * @brief CinS tensor packing layout parameters.
 * Fits each ResNet20 tensor into a single ciphertext with 16384 slots:
 * - Stem & Group 1: 16 channels over n1=1024 (32x32)
 * - Group 2: 64 channels over n1=256 (16x16)
 * - Group 3: 256 channels over n1=64 (8x8)
 */
struct TensorShape {
  int tensor_wh;
  int channels;
  int log_n1;
  int chan_packing;
  int chan_groups;

  static TensorShape Create(int tensor_wh, int channels,
                            int slot_capacity = 16384) {
    int log_n1 = static_cast<int>(
        std::ceil(std::log2(static_cast<double>(tensor_wh + 2)) * 2.0)) - 1;
    int n1 = 1 << log_n1;
    int chan_packing = slot_capacity / n1;
    AssertTrue(chan_packing > 0,
               "CinS capacity cannot fit tensor_wh and n1");
    int chan_groups = (channels + chan_packing - 1) / chan_packing;
    AssertTrue(chan_groups == 1,
               "LogN15 ResNet20 requires every tensor to fit in 1 ciphertext");
    return TensorShape{tensor_wh, channels, log_n1, chan_packing, chan_groups};
  }
};

/**
 * @brief Bit-reversal permutation table for logN.
 */
inline std::vector<int> BitReverse(int log_n) {
  int n = 1 << log_n;
  std::vector<int> table(n, 0);
  for (int i = 0; i < n; ++i) {
    int rev = 0;
    for (int bit = 0; bit < log_n; ++bit) {
      if ((i >> bit) & 1) {
        rev |= (1 << (log_n - 1 - bit));
      }
    }
    table[i] = rev;
  }
  return table;
}

/**
 * @brief PolyFrom2dTensor encodes a 2D spatial tensor into the folded real-in-complex
 * CinS polynomial form on the (tensor_wh + 2)-padded grid.
 */
inline std::vector<Complex> PolyFrom2dTensor(
    const std::vector<std::vector<double>> &matrix, int tensor_wh, int n1) {
  int padded = tensor_wh + 2;
  std::vector<Complex> before(2 * n1, Complex(0.0, 0.0));
  for (int i = 0; i < tensor_wh && i < static_cast<int>(matrix.size()); ++i) {
    for (int j = 0; j < tensor_wh && j < static_cast<int>(matrix[i].size()); ++j) {
      before[(i + 1) * padded + (j + 1)] = Complex(matrix[i][j], 0.0);
    }
  }
  std::vector<Complex> poly(n1);
  for (int i = 0; i < n1; ++i) {
    poly[i] = before[i] + Complex(0.0, 1.0) * before[i + n1];
  }
  return poly;
}

/**
 * @brief Decodes a folded CinS polynomial back to a 2D spatial tensor.
 */
inline std::vector<std::vector<double>> TensorFromPolyRealInCplx(
    const std::vector<Complex> &poly, int tensor_wh, int n1) {
  int padded = tensor_wh + 2;
  std::vector<std::vector<double>> tensor(
      tensor_wh, std::vector<double>(tensor_wh, 0.0));
  for (int i = 0; i < tensor_wh; ++i) {
    for (int j = 0; j < tensor_wh; ++j) {
      int idx = (i + 1) * padded + (j + 1);
      if (idx < n1) {
        tensor[i][j] = poly[idx].real();
      } else if (idx - n1 < n1) {
        tensor[i][j] = poly[idx - n1].imag();
      }
    }
  }
  return tensor;
}

/**
 * @brief PackChannels lays padded channel tensors into the bit-reversed slot vector.
 */
inline std::vector<Complex> PackChannels(
    const std::vector<std::vector<std::vector<double>>> &channels,
    const TensorShape &shape, int slot_count = 16384) {
  int n1 = 1 << shape.log_n1;
  auto brv = BitReverse(shape.log_n1);
  std::vector<Complex> slots(slot_count, Complex(0.0, 0.0));
  for (int ch = 0; ch < shape.chan_packing; ++ch) {
    std::vector<Complex> poly;
    if (ch < static_cast<int>(channels.size())) {
      poly = PolyFrom2dTensor(channels[ch], shape.tensor_wh, n1);
    } else {
      poly.assign(n1, Complex(0.0, 0.0));
    }
    for (int i = 0; i < n1; ++i) {
      slots[ch * n1 + brv[i]] = poly[i];
    }
  }
  return slots;
}

/**
 * @brief UnpackChannels recovers per-channel 2D tensors from a decoded slot vector.
 */
inline std::vector<std::vector<std::vector<double>>> UnpackChannels(
    const std::vector<Complex> &slots, const TensorShape &shape) {
  int n1 = 1 << shape.log_n1;
  auto brv = BitReverse(shape.log_n1);
  std::vector<std::vector<std::vector<double>>> channels(shape.chan_packing);
  for (int ch = 0; ch < shape.chan_packing; ++ch) {
    std::vector<Complex> poly(n1);
    for (int i = 0; i < n1; ++i) {
      poly[i] = slots[ch * n1 + brv[i]];
    }
    channels[ch] = TensorFromPolyRealInCplx(poly, shape.tensor_wh, n1);
  }
  return channels;
}

/**
 * @brief ComplexSlotsToCoefficients folds complex slot values into the real
 * coefficient vector [re | im] used by coefficient-domain bias encoding.
 */
inline std::vector<double> ComplexSlotsToCoefficients(
    const std::vector<Complex> &values, int coefficient_capacity = 32768) {
  AssertTrue(2 * static_cast<int>(values.size()) <= coefficient_capacity,
             "Values require more coefficients than capacity");
  int slot_capacity = coefficient_capacity / 2;
  std::vector<double> coeffs(coefficient_capacity, 0.0);
  for (size_t i = 0; i < values.size(); ++i) {
    coeffs[i] = values[i].real();
    coeffs[i + slot_capacity] = values[i].imag();
  }
  return coeffs;
}

/**
 * @brief Undoes the post-StC bit-reversed slot order at full-slot level.
 */
inline std::vector<Complex> UnbrvSlots(const std::vector<Complex> &values,
                                      int log_slots = 14) {
  auto brv = BitReverse(log_slots);
  std::vector<Complex> out(values.size());
  for (size_t i = 0; i < values.size(); ++i) {
    out[i] = values[brv[i]];
  }
  return out;
}

}  // namespace cnn
}  // namespace cheddar
