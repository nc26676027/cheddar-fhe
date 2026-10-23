#include "extension/cnn/AveragePooling.h"

#include <vector>

#include "common/Assert.h"
#include "extension/cnn/TensorShape.h"

namespace cheddar {
namespace cnn {

namespace {

using Transform = LinearTransform<uint64_t>;

void BuildPoolMatrices(StripedMatrix &real_matrix, StripedMatrix &imag_matrix,
                       int slots) {
  const int poolInputWH = 8;
  const int poolInputLogN1 = 6;
  const int n1 = 1 << poolInputLogN1;
  const int headFeatureCount = 64;
  const int poolBlockStride = 64;
  const double coefficient = 1.0 / (2.0 * poolInputWH * poolInputWH);

  auto brv = BitReverse(poolInputLogN1);

  for (int ch = 0; ch < headFeatureCount; ++ch) {
    int target = ch * poolBlockStride;
    for (int row = 0; row < poolInputWH; ++row) {
      for (int col = 0; col < poolInputWH; ++col) {
        int grid = (row + 1) * (poolInputWH + 2) + (col + 1);
        int offset = 0;
        bool imaginary = false;
        if (grid < n1) {
          offset = brv[grid];
          imaginary = false;
        } else {
          offset = brv[grid - n1];
          imaginary = true;
        }
        int source = ch * poolBlockStride + offset;
        int d = (source - target + slots) % slots;

        if (real_matrix.find(d) == real_matrix.end()) {
          real_matrix[d].assign(slots, Complex(0.0, 0.0));
        }
        if (imag_matrix.find(d) == imag_matrix.end()) {
          imag_matrix[d].assign(slots, Complex(0.0, 0.0));
        }

        if (imaginary) {
          imag_matrix[d][target] += Complex(0.0, -coefficient);
        } else {
          real_matrix[d][target] += Complex(coefficient, 0.0);
        }
      }
    }
  }
}

}  // namespace

AveragePooling::AveragePooling(ConstContextPtr<uint64_t> context,
                               const BootParameter &profile)
    : context_(context), profile_(profile) {
  const int slots = profile.slots;
  StripedMatrix real_mat(slots, slots);
  StripedMatrix imag_mat(slots, slots);
  BuildPoolMatrices(real_mat, imag_mat, slots);

  int max_r = 0;
  for (const auto &[d, _] : real_mat) max_r = std::max(max_r, d);
  auto [bs_r, gs_r] = Transform::DefaultBSGSSplit(std::max(real_mat.GetNumDiag(), max_r + 1));
  real_transform_ = std::make_unique<LinearTransform<uint64_t>>(
      context, real_mat, 3, double(profile.q[3]), bs_r, gs_r, 0, 0);
  real_transform_->ReleaseCompactBackings();

  int max_i = 0;
  for (const auto &[d, _] : imag_mat) max_i = std::max(max_i, d);
  auto [bs_i, gs_i] = Transform::DefaultBSGSSplit(std::max(imag_mat.GetNumDiag(), max_i + 1));
  imag_transform_ = std::make_unique<LinearTransform<uint64_t>>(
      context, imag_mat, 3, double(profile.q[3]), bs_i, gs_i, 0, 0);
  imag_transform_->ReleaseCompactBackings();
}

void AveragePooling::AddRequiredRotations(EvkRequest &req) const {
  real_transform_->AddRequiredRotations(req);
  imag_transform_->AddRequiredRotations(req);
}

void AveragePooling::Pool(Ciphertext<uint64_t> &out,
                          const Ciphertext<uint64_t> &in,
                          const EvkMap<uint64_t> &keys) const {
  auto context = GetContext();
  AssertTrue(in.GetNP() == context->param_.LevelToNP(3),
             "AveragePooling expects Level 3 input");

  Ciphertext<uint64_t> conj;
  context->HConj(conj, in, keys.GetConjugationKey());

  Ciphertext<uint64_t> real_ct, imag_ct;
  context->Add(real_ct, in, conj);
  context->Sub(imag_ct, in, conj);

  Ciphertext<uint64_t> real_part, imag_part;
  real_transform_->Evaluate(context, real_part, real_ct, keys);
  imag_transform_->Evaluate(context, imag_part, imag_ct, keys);

  context->Add(out, real_part, imag_part);
  out.SetNumSlots(profile_.slots);
  out.SetScale(profile_.application_scale);
}

}  // namespace cnn
}  // namespace cheddar
