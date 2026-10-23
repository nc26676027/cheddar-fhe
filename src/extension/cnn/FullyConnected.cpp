#include "extension/cnn/FullyConnected.h"

#include <vector>

#include "common/Assert.h"

namespace cheddar {
namespace cnn {

namespace {

using Transform = LinearTransform<uint64_t>;

StripedMatrix BuildFCMatrix(const std::vector<double> &weights, int slots) {
  const int headFeatureCount = 64;
  const int headLogitCount = 10;
  AssertTrue(weights.size() == static_cast<size_t>(headFeatureCount * headLogitCount),
             "FC weights dimension mismatch");

  StripedMatrix mat(slots, slots);
  for (int out = 0; out < headLogitCount; ++out) {
    for (int ch = 0; ch < headFeatureCount; ++ch) {
      int from_slot = ch * 64;
      int to_slot = out;
      int d = (from_slot - to_slot + slots) % slots;

      if (mat.find(d) == mat.end()) {
        mat[d].assign(slots, Complex(0.0, 0.0));
      }
      mat[d][to_slot] += Complex(weights[out * headFeatureCount + ch], 0.0);
    }
  }
  return mat;
}

}  // namespace

FullyConnected::FullyConnected(ConstContextPtr<uint64_t> context,
                               const BootParameter &profile,
                               const std::vector<double> &weights,
                               const std::vector<double> &bias)
    : context_(context), profile_(profile) {
  const int slots = profile.slots;
  StripedMatrix fc_mat = BuildFCMatrix(weights, slots);

  const int dense_base = 64;
  fc_transform_ = std::make_unique<LinearTransform<uint64_t>>(
      context, fc_mat, 2, double(profile.q[2]), dense_base);
  fc_transform_->ReleaseCompactBackings();

  // Encode bias at Level 1, scale 2^43
  std::vector<Complex> bias_values(slots, Complex(0.0, 0.0));
  for (size_t i = 0; i < bias.size() && i < 10; ++i) {
    bias_values[i] = Complex(bias[i], 0.0);
  }
  context->encoder_.Encode(bias_pt_, 1, profile.application_scale, bias_values);
}

void FullyConnected::AddRequiredRotations(EvkRequest &req) const {
  fc_transform_->AddRequiredRotations(req);
}

void FullyConnected::Classify(Ciphertext<uint64_t> &out,
                              const Ciphertext<uint64_t> &pooled,
                              const EvkMap<uint64_t> &keys) const {
  auto context = GetContext();
  AssertTrue(pooled.GetNP() == context->param_.LevelToNP(2),
             "FullyConnected expects Level 2 pooled input");

  fc_transform_->Evaluate(context, out, pooled, keys);
  context->Add(out, out, bias_pt_);
  out.SetNumSlots(profile_.slots);
  out.SetScale(profile_.application_scale);
}

}  // namespace cnn
}  // namespace cheddar
