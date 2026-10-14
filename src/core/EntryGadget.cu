#include "core/EntryGadget.h"

#include <algorithm>
#include "Random.h"
#include "common/Assert.h"
#include "common/Basic.cuh"
#include "common/PrimeUtils.h"

namespace cheddar {
namespace {
constexpr int kBlock = 256;

template <typename word>
__global__ void LiftSecret(word* dst, const word* src, int n, word q, word p) {
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  auto value = basic::Normalize(src[i], q);
  dst[i] = src[i];
  dst[n + i] = value < 0 ? p - static_cast<word>(-value)
                         : static_cast<word>(value);
}

template <typename word>
__global__ void ExtractDigit(word* dst, const word* src, int n, int shift) {
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  word digit = (src[i] >> shift) & word(15);
  dst[i] = digit;
  dst[n + i] = digit;
}

template <typename word>
__global__ void EncryptDigit(word* b, const word* a, const word* secret,
                             const word* target, const word* error, int n,
                             word q, word p, word factor,
                             make_signed_t<word> qi,
                             make_signed_t<word> pi) {
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  word modulus = i < n ? q : p;
  auto inverse = i < n ? qi : pi;
  word value = basic::Sub(error[i],
      basic::MultMontgomery(a[i], secret[i], modulus, inverse), modulus);
  // P * 16^j * s_dense is zero in the P limb.
  if (i < n) value = basic::Add(value,
      basic::MultMontgomery(target[i], factor, q, qi), q);
  b[i] = value;
}

template <typename word>
__global__ void DigitMAC(word* b, word* a, const word* digit,
                         const word* kb, const word* ka, int n,
                         word q, word p, make_signed_t<word> qi,
                         make_signed_t<word> pi) {
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  word modulus = i < n ? q : p;
  auto inverse = i < n ? qi : pi;
  b[i] = basic::Add(b[i],
      basic::MultMontgomery(digit[i], kb[i], modulus, inverse), modulus);
  a[i] = basic::Add(a[i],
      basic::MultMontgomery(digit[i], ka[i], modulus, inverse), modulus);
}

template <typename word>
__global__ void RoundedModDown(word* dst, const word* src, int n,
                               word q, word p, word inv_p_mont,
                               make_signed_t<word> qi) {
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  // Subtract the centered P residue, then divide by P in Q0.
  auto residue = basic::Normalize(src[n + i], p);
  word lifted = residue < 0 ? q - static_cast<word>(-residue)
                            : static_cast<word>(residue);
  dst[i] = basic::MultMontgomery(basic::Sub(src[i], lifted, q),
                                 inv_p_mont, q, qi);
}

template <typename word>
__global__ void AddInput(word* dst, const word* input, int n, word q) {
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  dst[i] = basic::Add(dst[i], input[i], q);
}
}  // namespace

template <typename word>
EntryGadget<word>::EntryGadget(const Parameter<word>& normal)
    : param_(normal.log_degree_, normal.base_scale_, 0, {{1, 0}},
             {normal.main_primes_[0]}, normal.entry_primes_),
      ntt_(param_), digits_(0) {
  AssertTrue(sizeof(word) == 8 && normal.entry_primes_.size() == 1,
             "Entry gadget only supports the 64-bit experiment");
  for (word value = param_.main_primes_[0] - 1; value; value >>= kDigitBits) {
    ++digits_;
  }
}

template <typename word>
void EntryGadget<word>::DecomposeDigit(
    DeviceVector<word>& result, const DeviceVector<word>& coefficients,
    int index) const {
  AssertTrue(coefficients.size() == static_cast<size_t>(param_.degree_) &&
                 index >= 0 && index < digits_,
             "Entry digit input/index mismatch");
  result.resize(2 * param_.degree_);
  ExtractDigit<<<param_.degree_ / kBlock, kBlock>>>(
      result.data(), coefficients.data(), param_.degree_, index * kDigitBits);
}

template <typename word>
void EntryGadget<word>::PopulateKey(
    EvaluationKey<word>& key, const DvConstView<word>& dense_q0,
    const DvConstView<word>& sparse_q0, double sigma) const {
  int n = param_.degree_;
  word q = param_.main_primes_[0], p = param_.aux_primes_[0];
  NPInfo qnp(1, 0, 0), qpnp(1, 0, 1);
  AssertTrue(key.GetNP() == qpnp && key.GetBeta() == digits_ &&
                 dense_q0.TotalSize() == n && sparse_q0.TotalSize() == n,
             "Entry key shape mismatch");
  DeviceVector<word> dense(2 * n), sparse(2 * n), coeff(n), error(2 * n);
  auto cv = coeff.View();
  auto transform_secret = [&](DeviceVector<word>& dst,
                              const DvConstView<word>& src) {
    ntt_.INTT(cv, qnp, src);
    LiftSecret<<<n / kBlock, kBlock>>>(dst.data(), coeff.data(), n, q, p);
    auto view = dst.View(n);
    ntt_.NTT(view, qpnp, dst.ConstView(n), true);
  };
  transform_secret(dense, dense_q0);
  transform_secret(sparse, sparse_q0);
  HostVector<word> uniform(2 * n), errors(2 * n);
  std::vector<int> sample(n);
  word factor = p;
  auto qi = primeutil::InvModBase(q), pi = primeutil::InvModBase(p);
  for (int j = 0; j < digits_; ++j) {
    Random::SampleUniformWord(uniform.data(), n, word(0), q - 1);
    Random::SampleUniformWord(uniform.data() + n, n, word(0), p - 1);
    CopyHostToDevice(key.ax_[j], uniform);
    // A fresh full-ring error for every digit, shared across its Q/P limbs.
    if (sigma == 0) {
      std::fill(sample.begin(), sample.end(), 0);
    } else {
      Random::SampleDiscreteNormal(sample.data(), n, 0, sigma);
    }
    for (int i = 0; i < 2 * n; ++i) {
      word modulus = i < n ? q : p;
      int value = sample[i % n];
      word magnitude = static_cast<word>(value < 0 ? -value : value) % modulus;
      errors[i] = value < 0 && magnitude ? modulus - magnitude : magnitude;
    }
    CopyHostToDevice(error, errors);
    auto ev = error.View(n);
    ntt_.NTT(ev, qpnp, error.ConstView(n), true);
    EncryptDigit<<<2 * n / kBlock, kBlock>>>(
        key.bx_[j].data(), key.ax_[j].data(), sparse.data(), dense.data(),
        error.data(), n, q, p, primeutil::ToMontgomery(factor, q), qi, pi);
    factor = primeutil::MultMod(factor, word(16), q);
  }
  key.SetEntryBasis(q, p);
}

template <typename word>
void EntryGadget<word>::Switch(Ciphertext<word>& result,
                                const Ciphertext<word>& input,
                                const EvaluationKey<word>& key) const {
  int n = param_.degree_;
  word q = param_.main_primes_[0], p = param_.aux_primes_[0];
  NPInfo qnp(1, 0, 0), qpnp(1, 0, 1);
  AssertTrue(input.GetNP() == qnp && !input.HasRx() &&
                 key.GetNP() == qpnp && key.GetBeta() == digits_ &&
                 key.GetEntryQ() == q && key.GetEntryP() == p,
             "Entry gadget basis/key mismatch");
  DeviceVector<word> coeff(n), digit(2 * n), b(2 * n), a(2 * n);
  auto cv = coeff.View();
  ntt_.INTT(cv, qnp, input.AxConstView());
  cudaMemset(b.data(), 0, 2 * n * sizeof(word));
  cudaMemset(a.data(), 0, 2 * n * sizeof(word));
  auto qi = primeutil::InvModBase(q), pi = primeutil::InvModBase(p);
  for (int j = 0; j < digits_; ++j) {
    DecomposeDigit(digit, coeff, j);
    auto dv = digit.View(n);
    ntt_.NTT(dv, qpnp, digit.ConstView(n), true);
    DigitMAC<<<2 * n / kBlock, kBlock>>>(
        b.data(), a.data(), digit.data(), key.bx_[j].data(),
        key.ax_[j].data(), n, q, p, qi, pi);
  }
  // Preserve the input B before resizing so Switch also permits result==input.
  DeviceVector<word> original_b(n);
  cudaMemcpy(original_b.data(), input.bx_.data(), n * sizeof(word),
             cudaMemcpyDeviceToDevice);
  result.RemoveRx();
  result.ModifyNP(qnp);
  result.SetScale(input.GetScale());
  result.SetNumSlots(input.GetNumSlots());
  word inv_p = primeutil::ToMontgomery(primeutil::InvMod(p, q), q);
  auto down = [&](DeviceVector<word>& dst, DeviceVector<word>& src) {
    auto sv = src.View(n);
    ntt_.INTT(sv, qpnp, src.ConstView(n));
    RoundedModDown<<<n / kBlock, kBlock>>>(dst.data(), src.data(), n,
                                          q, p, inv_p, qi);
    auto view = dst.View();
    ntt_.NTT(view, qnp, dst.ConstView(), true);
  };
  down(result.bx_, b);
  down(result.ax_, a);
  AddInput<<<n / kBlock, kBlock>>>(result.bx_.data(), original_b.data(), n, q);
}

template class EntryGadget<uint32_t>;
template class EntryGadget<uint64_t>;
}  // namespace cheddar
