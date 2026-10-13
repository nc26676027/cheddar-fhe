#include "extension/LinearTransform.h"

#include <algorithm>

#include "common/Assert.h"
#include "common/Basic.cuh"
#include "common/CommonUtils.h"
#include "common/ConstantMemory.cuh"

namespace {
template <int Start, int End, int Inc = 1, class Func>
constexpr void constexpr_for(Func &&func) {
  if constexpr (Start < End) {
    func(std::integral_constant<decltype(Start), Start>());
    constexpr_for<Start + Inc, End, Inc>(std::forward<Func>(func));
  }
}
}  // namespace

namespace cheddar {
namespace kernel {

template <typename word>
struct GSPackedTables {
  const word *bx[16], *ax[16], *mx[128];
  word *dst_bx[8], *dst_ax[8];
  int shift[128];
  __device__ int FactorIndex(int term, int i) const { return i >> shift[term]; }
};

// Accept compression only when every encoded RNS word matches its block's
// representative. This is lossless; floating encoding artifacts cause fallback.
template <typename word>
__global__ void CompactPlaintextKernel(word *compact, const word *full,
                                        int size, int shift, uint32_t *mismatch) {
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= size) return;
  int first = (i >> shift) << shift;
  word representative = full[first];
  if (full[i] != representative) atomicOr(mismatch, 1u);
  if (i == first) compact[i >> shift] = representative;
}

// Fused kernel for KeyMult, MAC, and Aut in the baby step.
template <typename word, int num_accum_padded>
__global__ void BSFusedKernel(
    word **dst_bx, word **dst_ax, const word **mod_up, const word **key_bx,
    const word **key_ax, int num_accum, int num_rotations, const word *primes,
    const make_signed_t<word> *inv_primes, int num_q_primes, word *key_extra,
    const word *input_bx_pseudo_modup, word *galois_factors) {
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  int log_degree = cm_log_degree();
  int prime_index = (i >> log_degree);
  int x_idx = i & ((1 << log_degree) - 1);
  int mod_up_index = i;
  const word prime = primes[prime_index];
  const make_signed_t<word> montgomery = inv_primes[prime_index];
  word mod_up_a[num_accum_padded];
  for (int idx = 0; idx < num_accum; idx++) {
    mod_up_a[idx] = basic::StreamingLoad(mod_up[idx] + mod_up_index);
  }

  word input_bx_pseudo_modup_value = 0;
  if (prime_index < num_q_primes) {
    input_bx_pseudo_modup_value =
        basic::StreamingLoad(input_bx_pseudo_modup + i);
  }
  for (int k = 0; k < num_rotations; k++) {
    word galois_factor = galois_factors[k];
    auto dst_index = basic::BitReverse(x_idx, log_degree + 1) + 1;
    dst_index = dst_index * galois_factor - 1;
    dst_index = basic::BitReverse(dst_index, log_degree + 1);

    word res_bx_value = 0;
    word res_ax_value = 0;
    for (int j = 0; j < num_accum; j++) {
      int key_index = i;
      if (prime_index >= num_q_primes) {
        key_index += key_extra[k];
      }
      word mod_up_value = mod_up_a[j];
      word key_ax_value =
          basic::StreamingLoad(key_ax[j + k * num_accum] + key_index);
      word key_bx_value =
          basic::StreamingLoad(key_bx[j + k * num_accum] + key_index);
      word mult =
          basic::MultMontgomery(mod_up_value, key_bx_value, prime, montgomery);
      res_bx_value = basic::Add(res_bx_value, mult, prime);

      mult =
          basic::MultMontgomery(mod_up_value, key_ax_value, prime, montgomery);
      res_ax_value = basic::Add(res_ax_value, mult, prime);
    }
    if (prime_index < num_q_primes) {
      res_bx_value =
          basic::Add(res_bx_value, input_bx_pseudo_modup_value, prime);
    }
    dst_bx[k][dst_index + (prime_index << log_degree)] = res_bx_value;
    dst_ax[k][dst_index + (prime_index << log_degree)] = res_ax_value;
  }
}

// Fused kernel for plaintext multiplication and accumulation in the giant step.
template <typename word, int num_bs_padded>
__global__ void GSFusedKernel(word **dst_bx, word **dst_ax, const word **bx,
                              const word **ax, const word **mx, int num_bs,
                              int num_gs, const word *primes,
                              const make_signed_t<word> *inv_primes) {
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  int log_degree = cm_log_degree();
  int prime_index = (i >> log_degree);
  int bxax_index = i;
  int cx_index = i;

  const word prime = primes[prime_index];
  const make_signed_t<word> montgomery = inv_primes[prime_index];

  word bx_[num_bs_padded];
  word ax_[num_bs_padded];
  for (int idx = 0; idx < num_bs; idx++) {
    bx_[idx] = basic::StreamingLoad(bx[idx] + bxax_index);
    ax_[idx] = basic::StreamingLoad(ax[idx] + bxax_index);
  }

  for (int k = 0; k < num_gs; k++) {
    word res_bx = 0;
    word res_ax = 0;
    for (int j = 0; j < num_bs; j++) {
      if (mx[j + k * num_bs] == nullptr) continue;
      word mx_value = basic::StreamingLoad(mx[j + k * num_bs] + cx_index);
      word bx_value = bx_[j];
      word ax_value = ax_[j];
      word mult = basic::MultMontgomery(bx_value, mx_value, prime, montgomery);
      res_bx = basic::Add(res_bx, mult, prime);
      mult = basic::MultMontgomery(ax_value, mx_value, prime, montgomery);
      res_ax = basic::Add(res_ax, mult, prime);
    }
    dst_bx[k][i] = res_bx;
    dst_ax[k][i] = res_ax;
  }
}

// Constant-index accesses scalarize the cached baby-step values into registers.
// Retain their reuse across all giant steps instead of rereading them globally.
template <typename word, int num_bs_padded>
__global__ void GSRegisterKernel(
    const __grid_constant__ GSPackedTables<word> tables, int num_bs, int num_gs, const word *primes,
    const make_signed_t<word> *inv_primes) {
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  int prime_index = i >> cm_log_degree();
  word prime = primes[prime_index];
  auto inverse = inv_primes[prime_index];
  word cached_b[num_bs_padded], cached_a[num_bs_padded];
#pragma unroll
  for (int j = 0; j < num_bs_padded; ++j) {
    if (j < num_bs) {
      cached_b[j] = basic::StreamingLoad(tables.bx[j] + i);
      cached_a[j] = basic::StreamingLoad(tables.ax[j] + i);
    }
  }
  for (int k = 0; k < num_gs; ++k) {
    word sum_b = 0, sum_a = 0;
#pragma unroll
    for (int j = 0; j < num_bs_padded; ++j) {
      if (j >= num_bs) continue;
      const word *diagonal = tables.mx[j + k * num_bs];
      if (!diagonal) continue;
      word factor = basic::StreamingLoad(diagonal + tables.FactorIndex(j + k * num_bs, i));
      sum_b = basic::Add(sum_b, basic::MultMontgomery(
          cached_b[j], factor, prime, inverse), prime);
      sum_a = basic::Add(sum_a, basic::MultMontgomery(
          cached_a[j], factor, prime, inverse), prime);
    }
    tables.dst_bx[k][i] = sum_b;
    tables.dst_ax[k][i] = sum_a;
  }
}

}  // namespace kernel

template <typename word>
std::pair<int, int> LinearTransform<word>::DefaultBSGSSplit(int num_diag) {
  AssertTrue(num_diag >= 1, "Invalid number of diagonals for LinearTransform");
  if (num_diag <= 4) {
    return {num_diag, 1};
  }
  int bs = 1;
  switch (num_diag) {
    case 7:
    case 8:
    case 15:
    case 16:
      bs = 4;
      break;
    case 31:
    case 32:
    case 63:
    case 64:
      bs = 8;
      break;
    default:
      bs = 1 << DivCeil(Log2Ceil(num_diag), 2);
      break;
  }
  int gs = DivCeil(num_diag, bs);
  return {bs, gs};
}

template <typename word>
int LinearTransform<word>::DetermineStride(const StripedMatrix &matrix) const {
  int gcd_rot = 0;
  int max_rot = 0;
  int num_pt = 0;

  int width = matrix.GetWidth();

  for (const auto &[i, _] : matrix) {
    num_pt += 1;
    int rot = (i - pre_rotation_) % width;
    if (rot < 0) {
      rot += width;
    }
    if (gcd_rot == 0 && rot == 0) {
      gcd_rot = rot;
    } else {
      gcd_rot = GCD(gcd_rot, rot);
    }
    max_rot = Max(max_rot, rot);
  }
  AssertTrue(num_pt >= 1, "LinearTransform requires at least 1 plaintext");
  if (num_pt == 1 && gcd_rot == 0) {
    gcd_rot = 1;
  }
  AssertTrue(gcd_rot > 0, "Something went wrong during LinearTransform setup");
  int max_pt_dist = (bs_ * gs_ - 1) * gcd_rot;

  AssertTrue(max_rot <= max_pt_dist,
             "Incompatible matrix and LinearTransform parameters: bs (" +
                 std::to_string(bs_) + "), gs (" + std::to_string(gs_) +
                 "), pre_rotation (" + std::to_string(pre_rotation_) + ")");
  return gcd_rot;
}

template <typename word>
typename LinearTransform<word>::PlainHoistMap
LinearTransform<word>::ConstructPlainHoistMap(
    const StripedMatrix &matrix) const {
  int height = matrix.GetHeight();
  int width = matrix.GetWidth();

  AssertTrue(IsPowOfTwo(height),
             "LinearTransform requires power-of-two height");
  AssertTrue(IsPowOfTwo(width), "LinearTransform requires power-of-two width");
  AssertTrue(height == width, "LinearTransform requires square matrix");

  int gs_stride = stride_ * bs_;

  PlainHoistMap hoist_map;
  for (const auto &[i, diag] : matrix) {
    int rot = (i - pre_rotation_) % width;
    if (rot < 0) {
      rot += width;
    }
    int bs_rot = rot % gs_stride;
    int gs_rot = rot - bs_rot;
    if (hoist_map.find(gs_rot) == hoist_map.end()) {
      hoist_map.try_emplace(gs_rot, std::map<int, Message>());
    }
    hoist_map[gs_rot].try_emplace(bs_rot, height, 0);
    int offset = (gs_rot - additional_pt_rot_) % height;
    if (offset < 0) {
      offset += height;
    }
    for (int j = 0; j < height; j++) {
      // reverse gs rot
      hoist_map[gs_rot][bs_rot][(j + offset) % height] = diag[j];
    }
  }
  return hoist_map;
}

template <typename word>
typename LinearTransform<word>::PlainHoistMap
LinearTransform<word>::ConstructDenseHoistMap(const StripedMatrix &matrix,
                                              int dense_base) const {
  int height = matrix.GetHeight();
  int width = matrix.GetWidth();

  AssertTrue(IsPowOfTwo(height),
             "LinearTransform requires power-of-two height");
  AssertTrue(IsPowOfTwo(width), "LinearTransform requires power-of-two width");
  AssertTrue(height == width, "LinearTransform requires square matrix");
  AssertTrue(dense_base > 0, "dense_base must be positive");

  PlainHoistMap hoist_map;
  for (const auto &[i, diag] : matrix) {
    int rot = i % width;
    if (rot < 0) {
      rot += width;
    }
    int bs_rot = rot % dense_base;
    int gs_rot = (rot - bs_rot) % width;
    if (gs_rot < 0) {
      gs_rot += width;
    }
    if (hoist_map.find(gs_rot) == hoist_map.end()) {
      hoist_map.try_emplace(gs_rot, std::map<int, Message>());
    }
    hoist_map[gs_rot].try_emplace(bs_rot, height, 0);
    int offset = gs_rot;
    for (int j = 0; j < height; j++) {
      // reverse gs rot
      hoist_map[gs_rot][bs_rot][(j + offset) % height] = diag[j];
    }
  }
  return hoist_map;
}

template <typename word>
void LinearTransform<word>::ExtractBSIndices(const PlainHoistMap &hoist_map) {
  for (const auto &[_, bs_map] : hoist_map) {
    for (const auto &[bs_idx, _] : bs_map) {
      bs_indices_.insert(bs_idx);
    }
  }
}

template <typename word>
void LinearTransform<word>::CompilePlaintexts(ConstContextPtr<word> context,
                                              const PlainHoistMap &hoist_map) {
  for (const auto &[gs_idx, bs_map] : hoist_map) {
    hoist_pt_map_.try_emplace(gs_idx, std::map<int, Pt>{});
    gs_indices_.push_back(gs_idx);
    for (const auto &[bs_idx, message] : bs_map) {
      hoist_pt_map_.at(gs_idx).try_emplace(bs_idx, NPInfo(0, 0, 0));
      int num_p_primes = context->param_.alpha_;

      context->encoder_.Encode(hoist_pt_map_.at(gs_idx).at(bs_idx), pt_level_,
                               pt_scale_, message, num_p_primes);
      if (compact_plaintexts_ && hoist_map.size() <= 8 && bs_indices_.size() <= 16 &&
          static_cast<int>(message.size()) == context->param_.degree_ / 2) {
        int period = 0;
        for (int candidate = 1; candidate <= 1024; candidate *= 2) {
          bool matches = true;
          for (size_t j = candidate; j < message.size(); ++j) {
            if (message[j] != message[j % candidate]) { matches = false; break; }
          }
          if (matches) { period = candidate; break; }
        }
        if (period) {
          auto &full = hoist_pt_map_.at(gs_idx).at(bs_idx);
          int shift = context->param_.log_degree_ - Log2Ceil(2 * period);
          auto key = std::make_pair(gs_idx, bs_idx);
          int full_size = static_cast<int>(full.mx_.size());
          auto [it, inserted] = compact_pt_map_.try_emplace(key, full_size >> shift, shift);
          DeviceVector<uint32_t> mismatch(1);
          cudaMemset(mismatch.data(), 0, sizeof(uint32_t));
          kernel::CompactPlaintextKernel<word><<<DivCeil(full_size, 256), 256>>>(
              it->second.data.data(), full.mx_.data(), full_size, shift, mismatch.data());
          uint32_t host_mismatch = 0;
          AssertTrue(cudaMemcpy(&host_mismatch, mismatch.data(), sizeof(uint32_t),
                                cudaMemcpyDeviceToHost) == cudaSuccess,
                     "Plaintext compaction verification failed");
          if (host_mismatch) compact_pt_map_.erase(it);
        }
      }
    }
  }
  std::sort(gs_indices_.begin(), gs_indices_.end());
}

template <typename word>
void LinearTransform<word>::InitializeHoistPlaintexts(
    ConstContextPtr<word> context, const PlainHoistMap &hoist_map,
    bool suppress_bs_swap) {
  AssertTrue(hoist_map.size() > 0,
             "LinearTransform: hoist_map should not be empty");
  if (kOptimizeAutomorphism && hoist_map.size() == 1 &&
      hoist_map.begin()->first == 0 && !suppress_bs_swap) {
    // when gs = 1, swap bs and gs.
    PlainHoistMap optimized;

    for (const auto &[bs_idx, msg] : hoist_map.begin()->second) {
      int msg_size = msg.size();
      optimized.try_emplace(bs_idx, std::map<int, Message>{});

      optimized[bs_idx].try_emplace(0, msg_size);
      for (int i = 0; i < msg_size; i++) {
        optimized[bs_idx][0][(i + bs_idx) % msg_size] = msg.at(i);
      }
    }
    ExtractBSIndices(optimized);
    CompilePlaintexts(context, optimized);
  } else {
    ExtractBSIndices(hoist_map);
    CompilePlaintexts(context, hoist_map);
  }
  if (!cm_populated_) {
    PopulateConstantMemory(context->param_);
    cm_populated_ = true;
  }
}

template <typename word>
LinearTransform<word>::LinearTransform(ConstContextPtr<word> context,
                                       const StripedMatrix &matrix,
                                       int pt_level, double pt_scale)
    : context_{context},
      pt_level_{pt_level},
      pt_scale_{pt_scale},
      bs_{DefaultBSGSSplit(matrix.GetNumDiag()).first},
      gs_{DefaultBSGSSplit(matrix.GetNumDiag()).second},
      pre_rotation_{0},
      additional_pt_rot_{0},
      stride_{DetermineStride(matrix)} {
  InitializeHoistPlaintexts(context, ConstructPlainHoistMap(matrix));
}

template <typename word>
LinearTransform<word>::LinearTransform(ConstContextPtr<word> context,
                                       const StripedMatrix &matrix,
                                       int pt_level, double pt_scale, int bs,
                                       int gs, int pre_rotation /*= 0*/,
                                       int additional_pt_rot /*= 0*/,
                                       bool compact_plaintexts /*= true*/)
    : context_{context},
      pt_level_{pt_level},
      pt_scale_{pt_scale},
      bs_{bs},
      gs_{gs},
      pre_rotation_{pre_rotation},
      additional_pt_rot_{additional_pt_rot},
      stride_{DetermineStride(matrix)},
      compact_plaintexts_{compact_plaintexts} {
  InitializeHoistPlaintexts(context, ConstructPlainHoistMap(matrix));
}

template <typename word>
LinearTransform<word>::LinearTransform(ConstContextPtr<word> context,
                                       const StripedMatrix &matrix,
                                       int pt_level, double pt_scale,
                                       int dense_base)
    : context_{context},
      pt_level_{pt_level},
      pt_scale_{pt_scale},
      bs_{dense_base},
      gs_{DivCeil(matrix.GetWidth(), dense_base)},
      pre_rotation_{0},
      additional_pt_rot_{0},
      stride_{1} {
  InitializeHoistPlaintexts(context, ConstructDenseHoistMap(matrix, dense_base));
}

template <typename word>
bool LinearTransform<word>::IsUsingBSGS() const {
  return bs_ > 1 && gs_ > 1;
}

template <typename word>
int LinearTransform<word>::GetBS() const {
  return bs_;
}

template <typename word>
int LinearTransform<word>::GetGS() const {
  return gs_;
}

template <typename word>
int LinearTransform<word>::GetPreRotationAmount() const {
  return pre_rotation_;
}

template <typename word>
std::pair<int, int> LinearTransform<word>::CheckStrideMinKS() const {
  int non_zero_bs = 0;
  int bs_gcd = 0;
  int max_bs = 0;
  for (const auto &bs_idx : bs_indices_) {
    if (bs_idx == 0) continue;
    bs_gcd = GCD(bs_gcd, bs_idx);
    non_zero_bs += 1;
    max_bs = Max(max_bs, bs_idx);
  }
  AssertTrue(non_zero_bs * bs_gcd == max_bs,
             "MinKS does not apply to the given hoist map baby-step sequence");

  int non_zero_gs = 0;
  int gs_gcd = 0;
  int max_gs = 0;
  for (const auto &gs_idx : gs_indices_) {
    if (gs_idx == 0) continue;
    gs_gcd = GCD(gs_gcd, gs_idx);
    non_zero_gs += 1;
    max_gs = Max(max_gs, gs_idx);
  }
  AssertTrue(non_zero_gs * gs_gcd == max_gs,
             "MinKS does not apply to the given hoist map giant-step sequence");
  return {bs_gcd, gs_gcd};
}

template <typename word>
void LinearTransform<word>::BSFusedKeyMult(
    ConstContextPtr<word> context, std::map<int, Ct> &res,
    std::vector<Dv> &a_modup, const Ct &a_orig, const EvkMap<word> &keys,
    std::vector<int> &rotations, const Dv &input_bx_pseudo_modup) const {
  NPInfo a_orig_np = a_orig.GetNP();
  int num_main = a_orig_np.num_main_;
  int num_ter = a_orig_np.num_ter_;
  int num_aux = keys.GetRotationKey(rotations[0]).GetNP().num_aux_;
  auto &mod_switcher = context->mod_switch_handlers_.at(pt_level_);
  int prime_offset = mod_switcher.GetPrimeOffset();
  int beta = mod_switcher.GetBeta();

  for (auto &[_, accum] : res) {
    AssertTrue(&accum != &a_orig,
               "In-place operation is not supported for MultKeyNoModDown");
  }

  NPInfo np(num_main, num_ter, num_aux);

  for (auto &[_, accum] : res) {
    accum.RemoveRx();
    accum.ModifyNP(np);
    accum.SetScale(a_orig.GetScale());
    accum.SetNumSlots(a_orig.GetNumSlots());
  }

  int num_accum = 0;
  for (int i = 0; i < beta; i++) {
    if (!mod_switcher.IsChunkActive(i)) continue;
    num_accum++;
  }
  int num_accum_offset = beta - num_accum;
  int num_q_primes = a_orig.GetNP().GetNumQ();
  int num_rotations = rotations.size();

  HostVector<const word *> modup_ptrs(num_accum, nullptr);
  HostVector<const word *> key_a_ptrs(num_accum * num_rotations, nullptr);
  HostVector<const word *> key_b_ptrs(num_accum * num_rotations, nullptr);
  HostVector<word *> dst_b_ptrs(num_rotations, nullptr);
  HostVector<word *> dst_a_ptrs(num_rotations, nullptr);
  HostVector<word> key_extra(num_rotations, 0);
  HostVector<word> galois_factors_h(num_rotations, 0);

  for (int i = 0; i < num_rotations; i++) {
    for (int j = 0; j < num_accum; j++) {
      key_b_ptrs[i * num_accum + j] =
          keys.GetRotationKey(rotations[i]).bx_[j + num_accum_offset].data() +
          prime_offset * context->param_.degree_;
      key_a_ptrs[i * num_accum + j] =
          keys.GetRotationKey(rotations[i]).ax_[j + num_accum_offset].data() +
          prime_offset * context->param_.degree_;
    }
    const Evk &key = keys.GetRotationKey(rotations[i]);
    DvConstView<word> key_view = key.AxConstView(0, prime_offset);
    key_extra[i] = key_view.QSize() - num_q_primes * context->param_.degree_;

    dst_b_ptrs[i] = res.at(rotations[i]).bx_.data();
    dst_a_ptrs[i] = res.at(rotations[i]).ax_.data();

    int permute_amount = rotations[i];
    AssertTrue(permute_amount != -1,
               "Conjugate case should not be handled in BSFusedKeyMult.");
    AssertTrue(
        permute_amount >= 0 && permute_amount < context->param_.degree_ / 2,
        "Permute: Invalid permute amount");
    galois_factors_h[i] = context->param_.GetGaloisFactor(
        context->param_.degree_ / 2 - permute_amount);
  }
  for (int i = 0; i < num_accum; i++) {
    modup_ptrs[i] = a_modup[i + num_accum_offset].data();
  }
  int num_primes = np.GetNumTotal();

  DeviceVector<const word *> modup_d_ptrs(num_accum);
  DeviceVector<const word *> key_a_d_ptrs(num_accum * num_rotations);
  DeviceVector<const word *> key_b_d_ptrs(num_accum * num_rotations);
  DeviceVector<word *> dst_b_d_ptrs(num_rotations);
  DeviceVector<word *> dst_a_d_ptrs(num_rotations);
  DeviceVector<word> key_extra_d(num_rotations);
  DeviceVector<word> galois_factors(num_rotations);
  CopyHostToDevice(modup_d_ptrs, modup_ptrs);
  CopyHostToDevice(key_a_d_ptrs, key_a_ptrs);
  CopyHostToDevice(key_b_d_ptrs, key_b_ptrs);
  CopyHostToDevice(dst_b_d_ptrs, dst_b_ptrs);
  CopyHostToDevice(dst_a_d_ptrs, dst_a_ptrs);
  CopyHostToDevice(key_extra_d, key_extra);
  CopyHostToDevice(galois_factors, galois_factors_h);

  const word *primes = context->param_.GetPrimesPtr(np);
  const make_signed_t<word> *inv_primes = context->param_.GetInvPrimesPtr(np);
  dim3 block_dim(kernel_block_dim_);
  dim3 grid_dim(num_primes * context->param_.degree_ / kernel_block_dim_);

  AssertTrue(num_accum <= (1 << max_log_beta_),
             "num_accum should not be greater than " +
                 std::to_string(1 << max_log_beta_));
  constexpr_for<0, max_log_beta_ + 1>([&](auto i) {
    constexpr int num_accum_padded = 1 << i;
    if (num_accum > num_accum_padded) return;
    if constexpr (i > 0) {
      if (num_accum <= (1 << (i - 1))) return;
    }
    kernel::BSFusedKernel<word, num_accum_padded><<<grid_dim, block_dim>>>(
        dst_b_d_ptrs.data(), dst_a_d_ptrs.data(), modup_d_ptrs.data(),
        key_b_d_ptrs.data(), key_a_d_ptrs.data(), num_accum, num_rotations,
        primes, inv_primes, num_q_primes, key_extra_d.data(),
        input_bx_pseudo_modup.data(), galois_factors.data());
  });
}

template <typename word>
void LinearTransform<word>::GSFusedPAccum(ConstContextPtr<word> context,
                                          std::map<int, Ct> &results,
                                          const std::vector<int> &gs_indices,
                                          const std::map<int, Ct> &bs) const {
  const Ct &first_ct = bs.begin()->second;
  const Pt &first_pt = hoist_pt_map_.at(gs_indices.front()).begin()->second;
  double scale = first_ct.GetScale() * first_pt.GetScale();
  int num_slots = Max(first_ct.GetNumSlots(), first_pt.GetNumSlots());
  int num_q_primes = first_ct.GetNP().GetNumQ();
  int num_p_primes = first_ct.GetNP().num_aux_;
  NPInfo np = first_ct.GetNP();
  for (auto giant_step : gs_indices) {
    const std::map<int, Pt> &pt_map = hoist_pt_map_.at(giant_step);
    for (const auto &[bs_idx, pt] : pt_map) {
      const auto &ct = bs.at(bs_idx);
      AssertTrue(num_q_primes == ct.GetNP().GetNumQ(),
                 "LinearTransform: number of q primes mismatch");
      AssertTrue(num_p_primes == ct.GetNP().num_aux_,
                 "LinearTransform: number of p primes mismatch");
      context->AssertSameScale(scale, ct.GetScale() * pt.GetScale());
      num_slots = Max(num_slots, pt.GetNumSlots(), ct.GetNumSlots());
    }
  }
  for (auto &res : results) {
    res.second.RemoveRx();
    res.second.ModifyNP(np);
    res.second.SetScale(scale);
    res.second.SetNumSlots(num_slots);
  }

  int num_bs = bs_indices_.size();
  int num_gs = gs_indices.size();
  HostVector<const word *> bx_ptrs(num_bs);
  HostVector<const word *> ax_ptrs(num_bs);
  HostVector<const word *> mx_ptrs(num_gs * num_bs);
  HostVector<word *> dst_b_ptrs(num_gs);
  HostVector<word *> dst_a_ptrs(num_gs);
  int num_primes = np.GetNumTotal();

  int idx = 0;
  for (auto bs_idx : bs_indices_) {
    const auto &ct = bs.at(bs_idx);
    bx_ptrs[idx] = ct.bx_.data();
    ax_ptrs[idx] = ct.ax_.data();
    idx++;
  }

  idx = 0;
  for (auto gs_idx : gs_indices) {
    const auto &pt_map = hoist_pt_map_.at(gs_idx);
    for (auto bs_idx : bs_indices_) {
      if (pt_map.find(bs_idx) == pt_map.end()) {
        mx_ptrs[idx] = nullptr;
      } else {
        mx_ptrs[idx] = pt_map.at(bs_idx).mx_.data();
      }
      idx++;
    }
  }

  idx = 0;
  for (const auto gs_idx : gs_indices) {
    auto &res = results.at(gs_idx);
    dst_b_ptrs[idx] = res.bx_.data();
    dst_a_ptrs[idx] = res.ax_.data();
    idx++;
  }

  // Small FFT folds fit in the kernel argument buffer. Avoid five temporary
  // device pointer tables and their host-to-device transfers on every call.
  if (compact_plaintexts_ && num_bs <= 16 && num_gs <= 8) {
    kernel::GSPackedTables<word> tables{};
    for (int j = 0; j < num_bs; ++j) {
      tables.bx[j] = bx_ptrs[j];
      tables.ax[j] = ax_ptrs[j];
    }
    int term = 0;
    for (int gs_idx : gs_indices) {
      for (int bs_idx : bs_indices_) {
        tables.mx[term] = mx_ptrs[term];
        auto compact = compact_pt_map_.find({gs_idx, bs_idx});
        if (compact != compact_pt_map_.end()) {
          tables.mx[term] = compact->second.data.data();
          tables.shift[term] = compact->second.shift;
        }
        ++term;
      }
    }
    for (int k = 0; k < num_gs; ++k) {
      tables.dst_bx[k] = dst_b_ptrs[k];
      tables.dst_ax[k] = dst_a_ptrs[k];
    }
    constexpr_for<0, 5>([&](auto i) {
      constexpr int padded = 1 << i;
      if (num_bs > padded) return;
      if constexpr (i > 0) if (num_bs <= (1 << (i - 1))) return;
      kernel::GSRegisterKernel<word, padded><<<
          num_primes * context->param_.degree_ / kernel_block_dim_, kernel_block_dim_>>>(
          tables, num_bs, num_gs, context->param_.GetPrimesPtr(np),
          context->param_.GetInvPrimesPtr(np));
    });
    return;
  }

  DeviceVector<const word *> bx_d_ptrs(num_bs);
  DeviceVector<const word *> ax_d_ptrs(num_bs);
  DeviceVector<const word *> mx_d_ptrs(num_gs * num_bs);
  DeviceVector<word *> dst_b_d_ptrs(num_gs);
  DeviceVector<word *> dst_a_d_ptrs(num_gs);
  CopyHostToDevice(bx_d_ptrs, bx_ptrs);
  CopyHostToDevice(ax_d_ptrs, ax_ptrs);
  CopyHostToDevice(mx_d_ptrs, mx_ptrs);
  CopyHostToDevice(dst_b_d_ptrs, dst_b_ptrs);
  CopyHostToDevice(dst_a_d_ptrs, dst_a_ptrs);

  const word *primes = context->param_.GetPrimesPtr(np);
  const make_signed_t<word> *inv_primes = context->param_.GetInvPrimesPtr(np);
  dim3 block_dim(kernel_block_dim_);
  dim3 grid_dim(num_primes * context->param_.degree_ / kernel_block_dim_);

  constexpr_for<0, max_log_bs_ + 1>([&](auto i) {
    constexpr int num_bs_padded = 1 << i;
    if (num_bs > num_bs_padded) return;
    if constexpr (i > 0) {
      if (num_bs <= (1 << (i - 1))) return;
    }
    kernel::GSFusedKernel<word, num_bs_padded><<<grid_dim, block_dim>>>(
        dst_b_d_ptrs.data(), dst_a_d_ptrs.data(), bx_d_ptrs.data(),
        ax_d_ptrs.data(), mx_d_ptrs.data(), num_bs, num_gs, primes, inv_primes);
  });
}

template <typename word>
void LinearTransform<word>::EvaluateSingleAccum(ConstContextPtr<word> context,
                                                Ct &res,
                                                const std::map<int, Ct> &bs,
                                                const std::map<int, Pt> &pt_map,
                                                bool inplace) const {
  std::vector<std::vector<DvConstView<word>>> bs_bx_ax_view;
  std::vector<DvConstView<word>> pt_mx_view;

  double scale = 0;
  int num_slots = 0;

  NPInfo np;
  bool first = true;
  for (const auto &[bs_idx, pt] : pt_map) {
    const auto &ct = bs.at(bs_idx);
    if (first) {
      scale = ct.GetScale() * pt.GetScale();
      np = ct.GetNP();
      first = false;
    } else {
      context->AssertSameScale(scale, ct.GetScale() * pt.GetScale());
      AssertTrue(np.GetNumQ() == ct.GetNP().GetNumQ(),
                 "LinearTransform: number of q primes mismatch");
      AssertTrue(np.num_aux_ == ct.GetNP().num_aux_,
                 "LinearTransform: number of p primes mismatch");
    }
    num_slots = Max(num_slots, pt.GetNumSlots(), ct.GetNumSlots());

    bs_bx_ax_view.push_back(ct.ConstViewVector());
    pt_mx_view.push_back(pt.ConstView());
  }

  res.RemoveRx();
  res.ModifyNP(np);
  res.SetScale(scale);
  res.SetNumSlots(num_slots);
  std::vector<DvView<word>> res_bx_ax_view = res.ViewVector();
  if (inplace) {
    bs_bx_ax_view.push_back(res.ConstViewVector());
  }

  context->elem_handler_.PAccum(res_bx_ax_view, np, bs_bx_ax_view, pt_mx_view);
}

template <typename word>
void LinearTransform<word>::EvaluateFinalModDown(ConstContextPtr<word> context,
                                                 Ct &res, Ct &final_accum,
                                                 int input_num_slots,
                                                 double input_scale) const {
  auto &mod_switcher = context->mod_switch_handlers_.at(pt_level_);

  int num_slots = input_num_slots;
  for (auto &[_, pt_map] : hoist_pt_map_) {
    for (auto &[_, pt] : pt_map) {
      num_slots = Max(num_slots, pt.GetNumSlots());
    }
  }
  final_accum.SetNumSlots(num_slots);
  final_accum.SetScale(pt_scale_ * input_scale);
  mod_switcher.ModDownAndRescale(res, final_accum);
}

template <typename word>
void LinearTransform<word>::EvaluateMinKSBabyStep(
    ConstContextPtr<word> context, std::map<int, Ct> &bs, const Ct &input,
    const EvkMap<word> &evk_map) const {
  auto [bs_stride, _] = CheckStrideMinKS();
  AssertTrue(bs.empty(), "LinearTransform: bs should be empty");
  for (const auto &bs_idx : bs_indices_) {
    bs.try_emplace(bs_idx, NPInfo(0, 0, 0));
    if (bs_idx == 0) {
      context->Copy(bs[0], input);
    } else {
      int prev_bs_idx = bs_idx - bs_stride;
      AssertTrue(bs.find(prev_bs_idx) != bs.end(),
                 "LinearTransform: MinKS baby-step sequence is not complete");
      context->HRot(bs[bs_idx], bs[prev_bs_idx],
                    evk_map.GetRotationKey(bs_stride), bs_stride);
    }
  }
}

template <typename word>
void LinearTransform<word>::EvaluateMinKSGiantStep(
    ConstContextPtr<word> context, Ct &res, const std::map<int, Ct> &bs,
    const EvkMap<word> &evk_map) const {
  AssertFalse(bs.empty(), "LinearTransform: bs should not be empty");

  auto [_, gs_stride] = CheckStrideMinKS();
  Ct accum;
  int prev_gs_idx = 0;
  bool first = true;
  for (auto it = hoist_pt_map_.rbegin(); it != hoist_pt_map_.rend(); it++) {
    int gs_idx = it->first;
    const auto &pt_map = it->second;
    EvaluateSingleAccum(context, accum, bs, pt_map, !first);
    if (!first) {
      AssertTrue(prev_gs_idx == gs_idx + gs_stride,
                 "LinearTransform: MinKS giant-step sequence is not complete");
    }
    if (gs_idx != 0) {
      context->HRot(accum, accum, evk_map.GetRotationKey(gs_stride), gs_stride);
    } else {
      AssertTrue(first || prev_gs_idx == gs_stride,
                 "LinearTransform: MinKS giant-step sequence is not complete");
    }
    first = false;
    prev_gs_idx = gs_idx;
  }
  context->Rescale(res, accum);
}

template <typename word>
void LinearTransform<word>::Evaluate(Ct &res, const Ct &input,
                                     const EvkMap<word> &evk_map,
                                     bool min_ks) const {
  Evaluate(GetContext(), res, input, evk_map, min_ks);
}

template <typename word>
void LinearTransform<word>::Evaluate(ConstContextPtr<word> context, Ct &res,
                                     const Ct &input,
                                     const EvkMap<word> &evk_map,
                                     bool min_ks) const {
  std::map<int, Ct> bs;
  EvaluateBabyStep(context, bs, input, evk_map, min_ks);
  EvaluateGiantStep(context, res, bs, evk_map, min_ks);
}

template <typename word>
void LinearTransform<word>::EvaluateBabyStep(ConstContextPtr<word> context,
                                             std::map<int, Ct> &bs,
                                             const Ct &input,
                                             const EvkMap<word> &evk_map,
                                             bool min_ks) const {
  NPInfo input_np = input.GetNP();
  int num_main_primes = input_np.num_main_;
  int num_ter_primes = input_np.num_ter_;
  int num_q_primes = input_np.GetNumQ();
  int num_p_primes = context->param_.alpha_;
  int degree = context->param_.degree_;
  AssertTrue(num_q_primes == context->param_.LevelToNP(pt_level_).GetNumQ(),
             "LinearTransform: input level mismatch");
  AssertTrue(input.GetNP().num_aux_ == 0,
             "LinearTransform: input should be mod-down");
  AssertFalse(input.HasRx(), "LinearTransform: input should be relinearized");

  if (bs_indices_.size() == 1 && *bs_indices_.begin() == 0) {
    bs.try_emplace(0, NPInfo(num_main_primes, num_ter_primes, 0));
    context->Copy(bs[0], input);
    return;
  }

  if (min_ks) {
    EvaluateMinKSBabyStep(context, bs, input, evk_map);
    return;
  }

  auto &mod_switcher = context->mod_switch_handlers_.at(pt_level_);
  int prime_offset = mod_switcher.GetPrimeOffset();
  int beta = mod_switcher.GetBeta();
  AssertTrue(bs.empty(), "LinearTransform: bs should be empty");

  // 1. ModUp
  std::vector<Dv> tmp_modup;
  mod_switcher.ModUp(tmp_modup, input.AxConstView());

  // 2. Baby-step rotations
  Dv *input_bx_pseudo_modup;
  DvConstView<word> p_prod_view(context->p_prod_.data() + prime_offset,
                                num_q_primes);
  Dv pseudo_modup_tmp;

  NPInfo modup_np(num_main_primes, num_ter_primes, num_p_primes);

  if (bs_indices_.find(0) == bs_indices_.end()) {
    pseudo_modup_tmp.resize(num_q_primes * degree);
    DvView<word> pseudo_modup_tmp_view = pseudo_modup_tmp.View();
    mod_switcher.PseudoModUp(pseudo_modup_tmp_view, input.BxConstView(),
                             p_prod_view);
    pseudo_modup_tmp.ZeroExtend(num_p_primes * degree);
    input_bx_pseudo_modup = &pseudo_modup_tmp;
  } else {
    bs.try_emplace(0, input_np);
    bs[0].SetScale(input.GetScale());
    bs[0].SetNumSlots(input.GetNumSlots());
    DvView<word> bs_0_bx_view = bs[0].BxView();
    DvView<word> bs_0_ax_view = bs[0].AxView();
    mod_switcher.PseudoModUp(bs_0_bx_view, input.BxConstView(), p_prod_view);
    mod_switcher.PseudoModUp(bs_0_ax_view, input.AxConstView(), p_prod_view);
    bs[0].bx_.ZeroExtend(num_p_primes * degree);
    bs[0].ax_.ZeroExtend(num_p_primes * degree);
    bs[0].ModifyNP(modup_np);
    input_bx_pseudo_modup = &(bs[0].bx_);
  }

  bool can_fuse_bs = beta <= (1 << max_log_beta_);
  if (kFuseBSKeyMult && can_fuse_bs) {
    std::vector<int> rotations;
    for (const auto &bs_idx : bs_indices_) {
      if (bs_idx != 0) {
        bs.try_emplace(bs_idx, modup_np);
        rotations.push_back(bs_idx);
      }
    }
    BSFusedKeyMult(context, bs, tmp_modup, input, evk_map, rotations,
                   *input_bx_pseudo_modup);
  } else {
    Ct tmp(modup_np);

    for (const auto &bs_idx : bs_indices_) {
      if (bs_idx != 0) {
        const auto &key = evk_map.GetRotationKey(bs_idx);
        bs.try_emplace(bs_idx, modup_np);

        context->MultKeyNoModDown(tmp, tmp_modup, input, key);
        DvView<word> tmp_bx_q_view(tmp.bx_.data(), num_q_primes * degree, 0);
        std::vector<DvView<word>> tmp_bx_view = {tmp_bx_q_view};
        std::vector<DvConstView<word>> tmp_bx_const_view = {tmp_bx_q_view};
        context->elem_handler_.Add(tmp_bx_view, input_np, tmp_bx_const_view,
                                   {(*input_bx_pseudo_modup).ConstView()});
        context->Permute(bs[bs_idx], tmp, bs_idx);
      }
    }
  }
}

template <typename word>
void LinearTransform<word>::EvaluateGiantStep(ConstContextPtr<word> context,
                                              Ct &res,
                                              const std::map<int, Ct> &bs,
                                              const EvkMap<word> &evk_map,
                                              bool min_ks) const {
  AssertFalse(bs.empty(), "LinearTransform: bs should not be empty");
  const Ct &ref_ct = bs.begin()->second;
  NPInfo ref_np = ref_ct.GetNP();
  int num_aux_primes = context->param_.alpha_;
  NPInfo q_prime_np(ref_np.num_main_, ref_np.num_ter_, 0);
  NPInfo q_p_prime_np(ref_np.num_main_, ref_np.num_ter_, num_aux_primes);
  auto &mod_switcher = context->mod_switch_handlers_.at(pt_level_);
  int prime_offset = mod_switcher.GetPrimeOffset();
  int num_q_primes = ref_np.GetNumQ();
  int input_num_slots = ref_ct.GetNumSlots();
  double input_scale = ref_ct.GetScale();
  int degree = context->param_.degree_;

  // when only gs indices exist
  if (bs_indices_.size() == 1 && *bs_indices_.begin() == 0) {
    AssertFalse(min_ks,
                "LinearTransform: min_ks should be false for bs == 1 case");
    const Ct &ct = bs.begin()->second;
    std::vector<Dv> ax_modup;
    mod_switcher.ModUp(ax_modup, ct.AxConstView());
    std::map<int, Ct> pt_mult;

    Dv *bx_pseudo = nullptr;
    Dv bx_pseudo_tmp;
    DvConstView<word> p_prod_view(context->p_prod_.data() + prime_offset,
                                  num_q_primes);
    if (hoist_pt_map_.find(0) == hoist_pt_map_.end()) {
      bx_pseudo_tmp.resize(num_q_primes * degree);
      DvConstView<word> ct_bx_view(ct.bx_.data(), num_q_primes * degree, 0);
      DvView<word> bx_pseudo_tmp_view = bx_pseudo_tmp.View();
      mod_switcher.PseudoModUp(bx_pseudo_tmp_view, ct_bx_view, p_prod_view);
      bx_pseudo = &bx_pseudo_tmp;
    }

    std::vector<std::vector<DvConstView<word>>> ct_bx_ax_view;
    std::vector<int> rot_indices;

    Ct final_accum;
    bool inplace = false;
    for (const auto &[gs_idx, pt_map] : hoist_pt_map_) {
      const auto &pt = pt_map.begin()->second;

      if (gs_idx == 0) {
        AssertFalse(inplace,
                    "LinearTransform: inplace should be false for gs_idx == 0");
        pt_mult.try_emplace(0, q_prime_np);
        DvConstView<word> ct_bx_view(ct.bx_.data(), num_q_primes * degree, 0);
        DvConstView<word> ct_ax_view(ct.ax_.data(), num_q_primes * degree, 0);
        DvView<word> final_accum_bx_view = pt_mult.at(0).BxView();
        DvView<word> final_accum_ax_view = pt_mult.at(0).AxView();
        mod_switcher.PseudoModUp(final_accum_bx_view, ct_bx_view, p_prod_view);
        mod_switcher.PseudoModUp(final_accum_ax_view, ct_ax_view, p_prod_view);
        pt_mult.at(0).bx_.ZeroExtend(num_aux_primes * degree);
        pt_mult.at(0).ax_.ZeroExtend(num_aux_primes * degree);
        pt_mult.at(0).ModifyNP(q_p_prime_np);
        bx_pseudo = &pt_mult.at(0).bx_;
        context->Mult(final_accum, pt_mult.at(0), pt);

        inplace = true;
        continue;
      }

      const auto &key = evk_map.GetRotationKey(gs_idx);

      pt_mult.try_emplace(gs_idx, q_p_prime_np);

      context->MultKeyNoModDown(pt_mult.at(gs_idx), ax_modup, ct, key);
      DvView<word> pt_mult_bx_view(pt_mult.at(gs_idx).bx_.data(),
                                   num_q_primes * degree, 0);
      std::vector<DvView<word>> pt_mult_bx_view_vec = {pt_mult_bx_view};
      std::vector<DvConstView<word>> bx_pseudo_view = {
          DvView<word>((*bx_pseudo).data(), num_q_primes * degree, 0)};
      context->elem_handler_.Add(pt_mult_bx_view_vec, q_prime_np,
                                 {pt_mult_bx_view}, bx_pseudo_view);
      context->Mult(pt_mult.at(gs_idx), pt_mult.at(gs_idx), pt);

      ct_bx_ax_view.push_back(
          {pt_mult.at(gs_idx).BxConstView(), pt_mult.at(gs_idx).AxConstView()});
      rot_indices.push_back(gs_idx);
    }

    if (!inplace) {
      final_accum.ModifyNP(q_p_prime_np);
      final_accum.SetScale(input_scale);
      final_accum.SetNumSlots(input_num_slots);
    }
    if (rot_indices.size() != 0) {
      auto final_accum_view = final_accum.ViewVector();
      auto final_accum_const_view = final_accum.ConstViewVector();
      if (inplace) {
        ct_bx_ax_view.push_back(final_accum_const_view);
      }
      context->elem_handler_.PermuteAccum(final_accum_view, q_p_prime_np,
                                          rot_indices, ct_bx_ax_view);
    }
    EvaluateFinalModDown(context, res, final_accum, input_num_slots,
                         input_scale);
    return;
  }

  if (min_ks) {
    EvaluateMinKSGiantStep(context, res, bs, evk_map);
    return;
  }

  if (kOptimizeAutomorphism) {
    EvaluateGiantStepOptimized(context, res, bs, evk_map);
    return;
  }

  Ct tmp, accum;
  if (gs_indices_.size() == 1 && gs_indices_.at(0) == 0) {
    EvaluateSingleAccum(context, accum, bs, hoist_pt_map_.begin()->second);
    EvaluateFinalModDown(context, res, accum, input_num_slots, input_scale);
    return;
  }

  Ct final_accum;
  bool final_accum_init = false;
  std::vector<Dv> tmp_modup;
  std::vector<DvView<word>> tmp_modup_view;
  mod_switcher.AllocateModUpBuffers(tmp_modup, tmp_modup_view);
  Dv tmp_moddown(num_q_primes * degree);
  for (const auto &[gs_idx, pt_map] : hoist_pt_map_) {
    if (gs_idx == 0 && final_accum_init == false) {
      EvaluateSingleAccum(context, final_accum, bs, pt_map);
      final_accum_init = true;
      continue;
    } else {
      EvaluateSingleAccum(context, accum, bs, pt_map);
    }

    if (gs_idx != 0) {
      DvView<word> tmp_moddown_view = tmp_moddown.View(0);
      mod_switcher.ModDown(tmp_moddown_view, accum.AxConstView());
      mod_switcher.ModUp(tmp_modup_view, tmp_moddown.ConstView());
      const auto &key = evk_map.GetRotationKey(gs_idx);
      context->MultKeyNoModDown(tmp, tmp_modup, accum, key);
      std::vector<DvView<word>> tmp_bx_view = {tmp.BxView()};

      context->elem_handler_.Add(tmp_bx_view, q_p_prime_np, {tmp.BxConstView()},
                                 {accum.BxConstView()});
      if (!final_accum_init) {
        context->Permute(final_accum, tmp, gs_idx);
        final_accum_init = true;
        continue;
      } else {
        context->Permute(accum, tmp, gs_idx);
      }
    }

    context->Add(final_accum, final_accum, accum);
  }
  EvaluateFinalModDown(context, res, final_accum, input_num_slots, input_scale);
}

template <typename word>
void LinearTransform<word>::EvaluateGiantStepOptimized(
    ConstContextPtr<word> context, Ct &res, const std::map<int, Ct> &bs,
    const EvkMap<word> &evk_map) const {
  AssertFalse(bs.empty(), "LinearTransform: bs should not be empty");
  const Ct &ref_ct = bs.begin()->second;

  NPInfo ref_ct_np = ref_ct.GetNP();
  int num_q_primes = ref_ct_np.GetNumQ();
  int input_num_slots = ref_ct.GetNumSlots();
  double input_scale = ref_ct.GetScale();
  int degree = context->param_.degree_;
  auto &mod_switcher = context->mod_switch_handlers_.at(pt_level_);

  Ct tmp;

  if (gs_indices_.size() == 1 && gs_indices_.at(0) == 0) {
    EvaluateSingleAccum(context, tmp, bs, hoist_pt_map_.begin()->second);
    EvaluateFinalModDown(context, res, tmp, input_num_slots, input_scale);
    return;
  }

  std::map<int, Ct> accum;
  bool gs_idx_0_exists = false;
  Ct *final_accum;
  accum.try_emplace(0, ref_ct_np);
  final_accum = &accum[0];

  if (hoist_pt_map_.begin() != hoist_pt_map_.end()) gs_idx_0_exists = true;
  for (const auto &[gs_idx, pt_map] : hoist_pt_map_) {
    accum.try_emplace(gs_idx, ref_ct_np);
  }

  bool can_fuse_gs = bs_indices_.size() <= (1 << max_log_bs_);
  if (kFuseGSPAccum && can_fuse_gs) {
    GSFusedPAccum(context, accum, gs_indices_, bs);
  } else {
    for (const auto &[gs_idx, pt_map] : hoist_pt_map_) {
      EvaluateSingleAccum(context, accum.at(gs_idx), bs, pt_map);
    }
  }

  std::vector<Dv> tmp_modup;
  std::vector<DvView<word>> tmp_modup_view;
  mod_switcher.AllocateModUpBuffers(tmp_modup, tmp_modup_view);
  Dv tmp_moddown(num_q_primes * degree);

  bool first = true;
  for (const auto &[gs_idx, ct] : accum) {
    if (gs_idx == 0) continue;

    DvView<word> tmp_moddown_view = tmp_moddown.View(0);
    mod_switcher.ModDown(tmp_moddown_view, ct.AxConstView());
    mod_switcher.ModUp(tmp_modup_view, tmp_moddown_view);
    const auto &key = evk_map.GetRotationKey(gs_idx);

    if (first && !gs_idx_0_exists) {
      context->MultKeyNoModDown(*final_accum, tmp_modup, ct, key);
      context->Permute(*final_accum, *final_accum, gs_idx);
    } else {
      context->MultKeyNoModDown(tmp, tmp_modup, ct, key);
      context->Permute(tmp, tmp, gs_idx);
      context->Add(*final_accum, *final_accum, tmp);
    }
    first = false;
  }

  std::vector<std::vector<DvConstView<word>>> ct_bx_view;
  std::vector<int> rot_indices;
  for (const auto &[gs_idx, ct] : accum) {
    if (gs_idx == 0) continue;
    ct_bx_view.push_back({ct.BxConstView()});
    rot_indices.push_back(gs_idx);
  }
  std::vector<DvView<word>> accum_view_vector = {(*final_accum).BxView()};

  ct_bx_view.push_back({(*final_accum).BxConstView()});
  context->elem_handler_.PermuteAccum(accum_view_vector, ref_ct_np, rot_indices,
                                      ct_bx_view);

  EvaluateFinalModDown(context, res, *final_accum, input_num_slots,
                       input_scale);
}

template <typename word>
void LinearTransform<word>::AddRequiredRotations(EvkRequest &req,
                                                 bool min_ks) const {
  if (min_ks) {
    auto [bs_stride, gs_stride] = CheckStrideMinKS();
    req.AddRequest(bs_stride, pt_level_);
    req.AddRequest(gs_stride, pt_level_);
    return;
  }

  for (const auto &bs_idx : bs_indices_) {
    if (bs_idx == 0) continue;
    req.AddRequest(bs_idx, pt_level_);
  }

  if (bs_indices_.size() == 1 && *bs_indices_.begin() == 0) {
    for (const auto &gs_idx : gs_indices_) {
      if (gs_idx == 0) continue;
      req.AddRequest(gs_idx, pt_level_);
    }
    return;
  }

  for (const auto &gs_idx : gs_indices_) {
    if (gs_idx == 0) continue;
    req.AddRequest(gs_idx, pt_level_);
  }
}

template class LinearTransform<uint32_t>;
template class LinearTransform<uint64_t>;

}  // namespace cheddar
