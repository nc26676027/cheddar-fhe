#include "extension/cnn/ConvStCFusion.h"

#include <cmath>
#include <vector>

#include "common/Assert.h"
#include "extension/MEBootMatrixPlanner.h"

namespace cheddar {
namespace cnn {

namespace {

using Transform = LinearTransform<uint64_t>;

StripedMatrix Merge(const std::vector<StripedMatrix> &stages, int begin,
                    int width) {
  auto result = stages.at(begin);
  for (int i = 1; i < width; ++i) {
    result = StripedMatrix::Mult(stages.at(begin + i), result);
  }
  return result;
}

Complex GenRoot(int base_nth, int lift_to) {
  double theta = (2.0 * M_PI * static_cast<double>(lift_to)) /
                 static_cast<double>(base_nth);
  return Complex(std::cos(theta), std::sin(theta));
}

Complex ComputeWrapAroundConst(int n1) {
  int acc = 0;
  int five_pow = 1;
  for (int i = 0; i < n1; ++i) {
    acc = (acc + five_pow) % (4 * n1);
    five_pow = (five_pow * 5) % (4 * n1);
  }
  return GenRoot(4 * n1, acc);
}

std::vector<Complex> PolyFrom2dKernel(
    const std::vector<std::vector<double>> &kernel_3x3, int size_padded_mat,
    int n1, Complex wrap_around_const) {
  std::vector<Complex> poly(n1, Complex(0.0, 0.0));
  int center_offset = size_padded_mat + 1;
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) {
      int idx = (n1 - (i * size_padded_mat + j) + center_offset) & (n1 - 1);
      poly[idx] = Complex(kernel_3x3[i][j], 0.0);
      if (idx >= n1 / 2) {
        poly[idx] *= wrap_around_const;
      }
    }
  }
  return poly;
}

using Kernel3x3 = std::vector<std::vector<double>>;
// [local_in][local_out] -> 3x3 kernel on the packed layout.
using KernelBlock = std::vector<std::vector<Kernel3x3>>;

KernelBlock MakeZeroBlock(int chan_packing) {
  return KernelBlock(chan_packing,
                     std::vector<Kernel3x3>(
                         chan_packing, Kernel3x3(3, std::vector<double>(3, 0.0))));
}

KernelBlock DirectKernelBlock(const ConvLayer &layer, int chan_packing) {
  auto block = MakeZeroBlock(chan_packing);
  for (int li = 0; li < chan_packing && li < layer.in_ch; ++li) {
    for (int lo = 0; lo < chan_packing && lo < layer.out_ch; ++lo) {
      block[li][lo] = layer.weight[lo][li];
    }
  }
  return block;
}

// Port of SplitInto2x2 for the single-group LogN15 layout: each pre-shape
// input channel becomes four quadrant channels (even/odd row x column).
KernelBlock SplitKernelBlock(const ConvLayer &layer, const TensorShape &pre,
                             const TensorShape &post) {
  AssertTrue(layer.in_ch <= pre.chan_packing,
             "Stride-2 input channels exceed pre-shape packing");
  AssertTrue(pre.chan_packing * 4 == post.chan_packing,
             "Split expansion factor must be 1 at LogN15");
  auto block = MakeZeroBlock(post.chan_packing);
  for (int li = 0; li < post.chan_packing; ++li) {
    const int split_type = li % 4;
    const int old_in = li / 4;
    if (old_in >= layer.in_ch) continue;
    for (int lo = 0; lo < post.chan_packing && lo < layer.out_ch; ++lo) {
      const auto &o = layer.weight[lo][old_in];
      auto &n = block[li][lo];
      switch (split_type) {
        case 0:
          n[1][1] = o[1][1];
          break;
        case 1:
          n[1][0] = o[1][0];
          n[1][1] = o[1][2];
          break;
        case 2:
          n[0][1] = o[0][1];
          n[1][1] = o[2][1];
          break;
        default:
          n[0][0] = o[0][0];
          n[0][1] = o[0][2];
          n[1][0] = o[2][0];
          n[1][1] = o[2][2];
          break;
      }
    }
  }
  return block;
}

std::vector<std::vector<Complex>> EncodeKernelToCinS(
    const KernelBlock &block, const TensorShape &shape, int slot_count = 16384) {
  int n1 = 1 << shape.log_n1;
  int chan_packing = shape.chan_packing;
  int size_padded_mat = shape.tensor_wh + 2;
  Complex wrap_const = ComputeWrapAroundConst(n1);

  std::vector<std::vector<Complex>> ker_part_ecd(
      chan_packing, std::vector<Complex>(slot_count, Complex(0.0, 0.0)));

  std::vector<int> pow_5_table(n1);
  int p5 = 1;
  for (int i = 0; i < n1; ++i) {
    pow_5_table[i] = p5;
    p5 = (p5 * 5) % (4 * n1);
  }

  for (int chan_diff = 0; chan_diff < chan_packing; ++chan_diff) {
    for (int in_chan = 0; in_chan < chan_packing; ++in_chan) {
      int out_chan = (in_chan + chan_diff) % chan_packing;
      const auto &k3x3 = block[in_chan][out_chan];
      bool nonzero = false;
      for (const auto &row : k3x3) {
        for (double v : row) nonzero |= (v != 0.0);
      }
      if (!nonzero) continue;
      auto poly_coeffs =
          PolyFrom2dKernel(k3x3, size_padded_mat, n1, wrap_const);
      std::vector<int> support;
      for (int k = 0; k < n1; ++k) {
        if (poly_coeffs[k] != Complex(0.0, 0.0)) support.push_back(k);
      }

      for (int pow_5_index = 0; pow_5_index < n1; ++pow_5_index) {
        Complex aggregated(0.0, 0.0);
        int p_val = pow_5_table[pow_5_index];
        for (int k : support) {
          aggregated += GenRoot(4 * n1, (p_val * k) % (4 * n1)) * poly_coeffs[k];
        }
        // 1/15 post-StC gain folded directly into kernel values
        ker_part_ecd[chan_diff][in_chan * n1 + pow_5_index] =
            aggregated * (1.0 / 15.0);
      }
    }
  }
  return ker_part_ecd;
}

}  // namespace

ConvStCStage::ConvStCStage(ConstContextPtr<uint64_t> context,
                           const TensorShape &shape,
                           const BootParameter &profile)
    : context_(context), shape_(shape), profile_(profile) {
  const int slots = profile.slots;
  auto stc_elementary =
      MEBootMatrixPlanner::GenerateElementaryStCStages(context, slots);

  middle_scale_ =
      std::exp2(profile.stc_intermediate_log_scale.value_or(37.875));
  stc_output_scale_ =
      std::exp2(profile.stc_output_log_scale.value_or(31.0));

  const int w0 = shape.log_n1;
  const int w1 = 14 - w0;

  // Factor 0 matrix
  base_factor0_matrix_ = Merge(stc_elementary, 0, w0);
  auto [bs0, gs0] = Transform::DefaultBSGSSplit(base_factor0_matrix_.GetNumDiag() + 1);
  const int pre0 = -(1 << w0);
  const int add0 = (1 << w0);
  const double pt_scale0 =
      double(profile.q[2]) * middle_scale_ / profile.application_scale;
  base_factor0_ = std::make_unique<LinearTransform<uint64_t>>(
      context, base_factor0_matrix_, 2, pt_scale0, bs0, gs0, pre0, add0);
  base_factor0_->ReleaseCompactBackings();

  // Factor 1 base matrix
  base_factor1_matrix_ = Merge(stc_elementary, w0, w1);

  // Shortcut Factor 0: shares factor 0
  shortcut_factor0_ = std::make_unique<LinearTransform<uint64_t>>(
      context, base_factor0_matrix_, 2, pt_scale0, bs0, gs0, pre0, add0);
  shortcut_factor0_->ReleaseCompactBackings();

  // Shortcut Factor 1: carries 1/15 post-StC gain
  auto shortcut_matrix = StripedMatrix::Mult(base_factor1_matrix_, 1.0 / 15.0);
  auto [bs1, gs1] = Transform::DefaultBSGSSplit(shortcut_matrix.GetNumDiag());
  const int pre1 = (1 << w0);
  const int add1 = 0;
  const double pt_scale1 =
      double(profile.q[1]) * stc_output_scale_ / middle_scale_;
  shortcut_factor1_ = std::make_unique<LinearTransform<uint64_t>>(
      context, shortcut_matrix, 1, pt_scale1, bs1, gs1, pre1, add1);
  shortcut_factor1_->ReleaseCompactBackings();
}

StripedMatrix ConvStCStage::BuildFusedKernelMatrix(const ConvLayer &layer) const {
  return FuseKernel(EncodeKernelToCinS(
      DirectKernelBlock(layer, shape_.chan_packing), shape_, profile_.slots));
}

StripedMatrix ConvStCStage::BuildSplitKernelMatrix(
    const ConvLayer &layer, const TensorShape &pre_shape) const {
  return FuseKernel(EncodeKernelToCinS(
      SplitKernelBlock(layer, pre_shape, shape_), shape_, profile_.slots));
}

StripedMatrix ConvStCStage::FuseKernel(
    const std::vector<std::vector<Complex>> &kernel_cins) const {
  const int slots = profile_.slots;
  const int n1 = 1 << shape_.log_n1;
  const int chan_packing = shape_.chan_packing;

  StripedMatrix fused(slots, slots);
  for (const auto &[diag_idx, base_diag] : base_factor1_matrix_) {
    std::vector<Complex> acc(slots, Complex(0.0, 0.0));
    for (int chann = 0; chann < chan_packing; ++chann) {
      int shift = n1 * chann;
      int shifted_idx = (diag_idx + shift) % slots;
      auto it = base_factor1_matrix_.find(shifted_idx);
      if (it != base_factor1_matrix_.end()) {
        const auto &shifted_diag = it->second;
        const auto &ker_vec = kernel_cins[chann];
        for (int k = 0; k < slots; ++k) {
          int col = (k + diag_idx) % slots;
          acc[k] += shifted_diag[k] * ker_vec[col];
        }
      }
    }
    fused[diag_idx] = std::move(acc);
  }
  return fused;
}

LinearTransform<uint64_t> ConvStCStage::CreateFusedKernelTransform(
    const ConvLayer &layer) const {
  return MakeFinalFactor(BuildFusedKernelMatrix(layer));
}

LinearTransform<uint64_t> ConvStCStage::CreateSplitKernelTransform(
    const ConvLayer &layer, const TensorShape &pre_shape) const {
  return MakeFinalFactor(BuildSplitKernelMatrix(layer, pre_shape));
}

LinearTransform<uint64_t> ConvStCStage::MakeFinalFactor(
    const StripedMatrix &fused_matrix) const {
  auto context = GetContext();
  auto [bs, gs] = Transform::DefaultBSGSSplit(fused_matrix.GetNumDiag());
  const int pre = (1 << shape_.log_n1);
  const int add = 0;
  const double pt_scale =
      double(profile_.q[1]) * stc_output_scale_ / middle_scale_;
  LinearTransform<uint64_t> transform(
      context, fused_matrix, 1, pt_scale, bs, gs, pre, add);
  transform.ReleaseCompactBackings();
  return transform;
}

Plaintext<uint64_t> ConvStCStage::CreateBiasPlaintext(
    const std::vector<double> &bias) const {
  auto context = GetContext();
  std::vector<std::vector<std::vector<double>>> channels(
      shape_.chan_packing,
      std::vector<std::vector<double>>(
          shape_.tensor_wh, std::vector<double>(shape_.tensor_wh, 0.0)));

  for (int local = 0; local < shape_.chan_packing; ++local) {
    double value = 0.0;
    if (local < static_cast<int>(bias.size())) {
      value = bias[local] * (1.0 / 15.0);
    }
    for (int r = 0; r < shape_.tensor_wh; ++r) {
      for (int c = 0; c < shape_.tensor_wh; ++c) {
        channels[local][r][c] = value;
      }
    }
  }

  auto slots = PackChannels(channels, shape_, profile_.slots);
  const size_t degree = context->param_.degree_;
  auto coeffs = ComplexSlotsToCoefficients(
      UnbrvSlots(slots, static_cast<int>(std::round(std::log2(profile_.slots)))),
      degree);

  const uint64_t q0 = profile_.q[0];
  std::vector<uint64_t> h_coeffs(degree);
  for (size_t i = 0; i < degree; ++i) {
    double scaled = std::round(coeffs[i] * stc_output_scale_);
    int64_t ival = static_cast<int64_t>(scaled);
    int64_t mval = ival % static_cast<int64_t>(q0);
    if (mval < 0) mval += q0;
    h_coeffs[i] = static_cast<uint64_t>(mval);
  }

  DeviceVector<uint64_t> d_coeffs(degree);
  auto err = cudaMemcpy(d_coeffs.data(), h_coeffs.data(),
                        degree * sizeof(uint64_t), cudaMemcpyHostToDevice);
  AssertTrue(err == cudaSuccess, "ConvStCStage: cudaMemcpy failed for bias coeffs");

  const auto np = context->param_.LevelToNP(0);
  Plaintext<uint64_t> pt(np);
  auto pt_view = pt.View();
  context->ntt_handler_.NTT(pt_view, np, d_coeffs.ConstView(), true);
  pt.SetScale(stc_output_scale_);
  pt.SetNumSlots(profile_.slots);
  return pt;
}

MaskPair ConvStCStage::CreateMaskPlaintexts() const {
  MaskPair masks;
  CreateMaskPlaintexts(masks.real, masks.imag);
  return masks;
}

void ConvStCStage::CreateMaskPlaintexts(
    Plaintext<uint64_t> &mask_real, Plaintext<uint64_t> &mask_imag) const {
  auto context = GetContext();
  const int n1 = 1 << shape_.log_n1;
  const int padded = shape_.tensor_wh + 2;

  std::vector<Complex> poly_real(n1, Complex(0.0, 0.0));
  std::vector<Complex> poly_imag(n1, Complex(0.0, 0.0));

  for (int i = 0; i < shape_.tensor_wh; ++i) {
    for (int j = 0; j < shape_.tensor_wh; ++j) {
      int idx = (i + 1) * padded + (j + 1);
      if (idx < n1) {
        poly_real[idx] = Complex(0.5, 0.0);
      } else if (idx >= n1 && idx - n1 < n1) {
        poly_imag[idx - n1] = Complex(0.5, 0.0);
      }
    }
  }

  auto brv = BitReverse(shape_.log_n1);
  std::vector<Complex> slots_real(profile_.slots, Complex(0.0, 0.0));
  std::vector<Complex> slots_imag(profile_.slots, Complex(0.0, 0.0));

  for (int ch = 0; ch < shape_.chan_packing; ++ch) {
    for (int i = 0; i < n1; ++i) {
      slots_real[ch * n1 + brv[i]] = poly_real[i];
      slots_imag[ch * n1 + brv[i]] = poly_imag[i];
    }
  }

  // RiC masks are encoded at Level 3 with scale q[3]
  context->encoder_.Encode(mask_real, 3, double(profile_.q[3]), slots_real);
  context->encoder_.Encode(mask_imag, 3, double(profile_.q[3]), slots_imag);
}

void ConvStCStage::AddRequiredRotations(EvkRequest &req) const {
  base_factor0_->AddRequiredRotations(req);
  shortcut_factor1_->AddRequiredRotations(req);
}

void ConvStCStage::HomTensorPolyMask(
    Ciphertext<uint64_t> &out, const Ciphertext<uint64_t> &in,
    const Plaintext<uint64_t> &mask_real,
    const Plaintext<uint64_t> &mask_imag,
    const EvkMap<uint64_t> &keys) const {
  auto context = GetContext();
  AssertTrue(in.GetNP() == context->param_.LevelToNP(3),
             "HomTensorPolyMask requires Level 3 input");

  Ciphertext<uint64_t> temp_real, temp_imag;
  context->Mult(temp_real, in, mask_real);
  context->Mult(temp_imag, in, mask_imag);

  Ciphertext<uint64_t> conj_real, conj_imag;
  context->HConj(conj_real, temp_real, keys.GetConjugationKey());
  context->HConj(conj_imag, temp_imag, keys.GetConjugationKey());

  Ciphertext<uint64_t> real_part, imag_part;
  context->Add(real_part, temp_real, conj_real);
  Ciphertext<uint64_t> rescaled_real;
  context->Rescale(rescaled_real, real_part);

  context->Sub(imag_part, temp_imag, conj_imag);
  Ciphertext<uint64_t> rescaled_imag;
  context->Rescale(rescaled_imag, imag_part);

  context->Add(out, rescaled_real, rescaled_imag);
  out.SetNumSlots(in.GetNumSlots());
  out.SetScale(profile_.application_scale);
}

void ConvStCStage::ConvStC(
    Ciphertext<uint64_t> &out, const Ciphertext<uint64_t> &in,
    const LinearTransform<uint64_t> &fused_kernel,
    const Plaintext<uint64_t> *bias, const MaskPair *masks,
    const EvkMap<uint64_t> &keys) const {
  ConvStC(out, in, fused_kernel, bias, masks ? &masks->real : nullptr,
          masks ? &masks->imag : nullptr, keys);
}

void ConvStCStage::ConvStC(
    Ciphertext<uint64_t> &out, const Ciphertext<uint64_t> &in,
    const LinearTransform<uint64_t> &fused_kernel,
    const Plaintext<uint64_t> *bias, const Plaintext<uint64_t> *mask_real,
    const Plaintext<uint64_t> *mask_imag,
    const EvkMap<uint64_t> &keys) const {
  auto context = GetContext();

  Ciphertext<uint64_t> masked;
  const Ciphertext<uint64_t> *work_ptr = &in;
  if (mask_real != nullptr && mask_imag != nullptr) {
    HomTensorPolyMask(masked, in, *mask_real, *mask_imag, keys);
    work_ptr = &masked;
  }

  AssertTrue(work_ptr->GetNP() == context->param_.LevelToNP(2),
             "ConvStC expects Level 2 input for StC Factor 0");

  Ciphertext<uint64_t> mid;
  base_factor0_->Evaluate(context, mid, *work_ptr, keys);
  context->AssertSameScale(mid, middle_scale_);

  Ciphertext<uint64_t> post;
  fused_kernel.Evaluate(context, post, mid, keys);
  context->AssertSameScale(post, stc_output_scale_);

  if (bias != nullptr) {
    context->Add(out, post, *bias);
  } else {
    out = std::move(post);
  }
  out.SetNumSlots(profile_.slots);
  out.SetScale(stc_output_scale_);
}

void ConvStCStage::ShortcutStC(Ciphertext<uint64_t> &out,
                              const Ciphertext<uint64_t> &in,
                              const EvkMap<uint64_t> &keys) const {
  auto context = GetContext();
  Ciphertext<uint64_t> dropped;
  const Ciphertext<uint64_t> *work_ptr = &in;
  int in_level = context->param_.NPToLevel(in.GetNP());
  if (in_level > 2) {
    context->DropLevel(dropped, in, 2);
    work_ptr = &dropped;
  }

  Ciphertext<uint64_t> mid;
  shortcut_factor0_->Evaluate(context, mid, *work_ptr, keys);
  context->AssertSameScale(mid, middle_scale_);

  shortcut_factor1_->Evaluate(context, out, mid, keys);
  context->AssertSameScale(out, stc_output_scale_);
  out.SetNumSlots(profile_.slots);
  out.SetScale(stc_output_scale_);
}

}  // namespace cnn
}  // namespace cheddar
