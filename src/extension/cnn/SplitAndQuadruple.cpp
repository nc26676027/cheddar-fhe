#include "extension/cnn/SplitAndQuadruple.h"

#include "common/Assert.h"

namespace cheddar {
namespace cnn {

SplitAndQuadruple::SplitAndQuadruple(ConstContextPtr<uint64_t> context,
                                     const TensorShape &pre_shape,
                                     const BootParameter &profile)
    : context_(context),
      pre_(pre_shape),
      post_(TensorShape::Create(pre_shape.tensor_wh / 2,
                                pre_shape.chan_packing * 4, profile.slots)),
      profile_(profile) {
  AssertTrue(pre_.chan_packing * 4 == post_.chan_packing,
             "SplitAndQuadruple expansion factor must be 1 at LogN15");
  // Plaintext scale q[3]: the rescale returns the 2^43 working scale.
  const double pt_scale = double(profile.q[3]);
  const int dense_base = 64;
  for (int part = 0; part < 2; ++part) {
    auto matrix = BuildMatrix(pre_, post_, profile.slots, part == 1);
    auto transform = std::make_unique<LinearTransform<uint64_t>>(
        context, matrix, 3, pt_scale, dense_base);
    (part == 0 ? direct_ : conjugate_) = std::move(transform);
  }
}

StripedMatrix SplitAndQuadruple::BuildMatrix(const TensorShape &pre,
                                             const TensorShape &post,
                                             int slots, bool conjugate_part) {
  const int old_n1 = 1 << pre.log_n1;
  const int new_n1 = 1 << post.log_n1;
  const int old_padded = pre.tensor_wh + 2;
  const int new_padded = post.tensor_wh + 2;
  const auto brv_old = BitReverse(pre.log_n1);
  const auto brv_new = BitReverse(post.log_n1);

  struct Part {
    int offset;
    Complex ct_low, conj_low, ct_high, conj_high;
  };
  const Part parts[2] = {
      {0, Complex(0.5, 0), Complex(0.5, 0), Complex(0, 0.5), Complex(0, 0.5)},
      {old_n1, Complex(0, -0.5), Complex(0, 0.5), Complex(0.5, 0),
       Complex(-0.5, 0)},
  };

  StripedMatrix matrix(slots, slots);
  for (int base = 0; base < pre.chan_packing; ++base) {
    for (int i_old_slot = 0; i_old_slot < old_n1; ++i_old_slot) {
      const int src = base * old_n1 + i_old_slot;
      for (const auto &part : parts) {
        const int poly_pos = brv_old[i_old_slot] + part.offset;
        const int i_old = poly_pos / old_padded - 1;
        const int j_old = poly_pos % old_padded - 1;
        if (i_old < 0 || i_old >= pre.tensor_wh || j_old < 0 ||
            j_old >= pre.tensor_wh) {
          continue;
        }
        const int quadrant_idx = (i_old % 2) * 2 + (j_old % 2);
        const int new_local = base * 4 + quadrant_idx;
        const int new_pos = (i_old / 2 + 1) * new_padded + (j_old / 2 + 1);
        int dst;
        Complex coefficient;
        if (new_pos < new_n1) {
          dst = new_local * new_n1 + brv_new[new_pos];
          coefficient = conjugate_part ? part.conj_low : part.ct_low;
        } else if (new_pos < 2 * new_n1) {
          dst = new_local * new_n1 + brv_new[new_pos - new_n1];
          coefficient = conjugate_part ? part.conj_high : part.ct_high;
        } else {
          continue;
        }
        const int d = (src - dst + slots) % slots;
        auto &diag = matrix[d];
        if (diag.empty()) diag.assign(slots, Complex(0.0, 0.0));
        diag[dst] += coefficient;
      }
    }
  }
  return matrix;
}

void SplitAndQuadruple::AddRequiredRotations(EvkRequest &req) const {
  direct_->AddRequiredRotations(req);
  conjugate_->AddRequiredRotations(req);
}

void SplitAndQuadruple::Evaluate(Ciphertext<uint64_t> &out,
                                 const Ciphertext<uint64_t> &in,
                                 const EvkMap<uint64_t> &keys) const {
  auto context = context_.lock();
  AssertTrue(context != nullptr, "Context has been destroyed");
  AssertTrue(in.GetNP() == context->param_.LevelToNP(3),
             "SplitAndQuadruple requires Level 3 input");
  Ciphertext<uint64_t> conj, part1, part2;
  context->HConj(conj, in, keys.GetConjugationKey());
  direct_->Evaluate(context, part1, in, keys);
  conjugate_->Evaluate(context, part2, conj, keys);
  context->Add(out, part1, part2);
  out.SetNumSlots(in.GetNumSlots());
  out.SetScale(profile_.application_scale);
}

}  // namespace cnn
}  // namespace cheddar
