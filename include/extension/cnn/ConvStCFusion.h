#pragma once

#include <memory>
#include <vector>

#include "core/Context.h"
#include "core/EvkMap.h"
#include "core/EvkRequest.h"
#include "extension/BootParameter.h"
#include "extension/LinearTransform.h"
#include "extension/StripedMatrix.h"
#include "extension/cnn/TensorShape.h"

namespace cheddar {
namespace cnn {

/**
 * @brief Representation of a 2D convolution layer weights and bias.
 */
struct ConvLayer {
  std::vector<std::vector<std::vector<std::vector<double>>>> weight;  // [out_ch][in_ch][3][3]
  std::vector<double> bias;                                          // [out_ch]
  int in_ch;
  int out_ch;
  int stride;
};

/**
 * @brief Paired real and imaginary masking plaintexts for Real-in-Complex CinS extraction.
 */
struct MaskPair {
  Plaintext<uint64_t> real;
  Plaintext<uint64_t> imag;
};

/**
 * @brief ConvStCStage manages the Slots-to-Coeffs factors for a specific tensor shape.
 * - Factor 0: elementary StC stages [0 .. log_n1 - 1] (Level 2 -> Level 1, middle scale)
 * - Factor 1: elementary StC stages [log_n1 .. 13] (Level 1 -> Level 0, output scale 2^31)
 * Fuses 2D convolution kernels directly into Factor 1, targeting Level 0 at scale 2^31
 * with 1/15 gain to match Func15RBootExecution::EvaluatePostStC.
 */
class ConvStCStage {
 public:
  ConvStCStage(ConstContextPtr<uint64_t> context, const TensorShape &shape,
               const BootParameter &profile);

  const TensorShape &GetShape() const { return shape_; }

  StripedMatrix BuildFusedKernelMatrix(const ConvLayer &layer) const;
  LinearTransform<uint64_t> CreateFusedKernelTransform(
      const ConvLayer &layer) const;

  /**
   * @brief Stride-2 kernel fused into Factor 1, consuming the 2x2-quadrant
   * packing produced by SplitAndQuadruple from `pre_shape`.
   */
  StripedMatrix BuildSplitKernelMatrix(const ConvLayer &layer,
                                       const TensorShape &pre_shape) const;
  LinearTransform<uint64_t> CreateSplitKernelTransform(
      const ConvLayer &layer, const TensorShape &pre_shape) const;

  Plaintext<uint64_t> CreateBiasPlaintext(
      const std::vector<double> &bias) const;

  MaskPair CreateMaskPlaintexts() const;
  void CreateMaskPlaintexts(Plaintext<uint64_t> &mask_real,
                            Plaintext<uint64_t> &mask_imag) const;

  void AddRequiredRotations(EvkRequest &req) const;

  void HomTensorPolyMask(Ciphertext<uint64_t> &out,
                         const Ciphertext<uint64_t> &in,
                         const Plaintext<uint64_t> &mask_real,
                         const Plaintext<uint64_t> &mask_imag,
                         const EvkMap<uint64_t> &keys) const;

  void ConvStC(Ciphertext<uint64_t> &out, const Ciphertext<uint64_t> &in,
               const LinearTransform<uint64_t> &fused_kernel,
               const Plaintext<uint64_t> *bias,
               const MaskPair *masks,
               const EvkMap<uint64_t> &keys) const;

  void ConvStC(Ciphertext<uint64_t> &out, const Ciphertext<uint64_t> &in,
               const LinearTransform<uint64_t> &fused_kernel,
               const Plaintext<uint64_t> *bias,
               const Plaintext<uint64_t> *mask_real,
               const Plaintext<uint64_t> *mask_imag,
               const EvkMap<uint64_t> &keys) const;

  void ShortcutStC(Ciphertext<uint64_t> &out, const Ciphertext<uint64_t> &in,
                   const EvkMap<uint64_t> &keys) const;

 private:
  std::weak_ptr<const Context<uint64_t>> context_;
  TensorShape shape_;
  BootParameter profile_;
  double middle_scale_;
  double stc_output_scale_;

  StripedMatrix base_factor0_matrix_;
  StripedMatrix base_factor1_matrix_;

  std::unique_ptr<LinearTransform<uint64_t>> base_factor0_;
  std::unique_ptr<LinearTransform<uint64_t>> shortcut_factor0_;
  std::unique_ptr<LinearTransform<uint64_t>> shortcut_factor1_;

  ConstContextPtr<uint64_t> GetContext() const {
    auto ctx = context_.lock();
    AssertTrue(ctx != nullptr, "Context has been destroyed");
    return ctx;
  }

  StripedMatrix FuseKernel(
      const std::vector<std::vector<Complex>> &kernel_cins) const;
  LinearTransform<uint64_t> MakeFinalFactor(
      const StripedMatrix &fused_matrix) const;
};

}  // namespace cnn
}  // namespace cheddar
