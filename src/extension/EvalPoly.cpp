#include "extension/EvalPoly.h"

#include <cmath>
#include <limits>
#include "common/Assert.h"
#include "common/CommonUtils.h"

namespace {

using namespace cheddar;

template <typename T>
void RemoveZero(std::vector<T> &coefficients, double threshold) {
  while (!coefficients.empty() &&
         Abs(coefficients.back()) < threshold) {
    coefficients.pop_back();
  }
  for (auto &c : coefficients) {
    if (Abs(c) < threshold) {
      c = 0;
    }
  }
}

template <typename T>
void AddPolynomial(std::vector<T> res, const std::vector<T> &a,
                   const std::vector<T> &b) {
  res.resize(std::max(a.size(), b.size()));
  std::fill(res.begin(), res.end(), 0);
  for (size_t i = 0; i < a.size(); ++i) {
    res[i] += a[i];
  }
  for (size_t i = 0; i < b.size(); ++i) {
    res[i] += b[i];
  }
}

template <typename T, typename C>
void AddPolynomial(std::vector<T> res, const std::vector<T> &a, const C &b) {
  res.resize(a.size());
  std::fill(res.begin(), res.end(), 0);
  for (size_t i = 0; i < a.size(); ++i) {
    res[i] += a[i];
  }
  res[0] += b;
}

template <typename T>
void SubPolynomial(std::vector<T> res, const std::vector<T> &a,
                   const std::vector<T> &b) {
  res.resize(std::max(a.size(), b.size()));
  std::fill(res.begin(), res.end(), 0);
  for (size_t i = 0; i < a.size(); ++i) {
    res[i] += a[i];
  }
  for (size_t i = 0; i < b.size(); ++i) {
    res[i] -= b[i];
  }
}

template <typename T, typename C>
void SubPolynomial(std::vector<T> res, const std::vector<T> &a, const C &b) {
  res.resize(a.size());
  std::fill(res.begin(), res.end(), 0);
  for (size_t i = 0; i < a.size(); ++i) {
    res[i] += a[i];
  }
  res[0] -= b;
}

template <typename T, typename C>
void SubPolynomial(std::vector<T> res, const C &b, const std::vector<T> &a) {
  res.resize(a.size());
  std::fill(res.begin(), res.end(), 0);
  for (size_t i = 0; i < a.size(); ++i) {
    res[i] -= a[i];
  }
  res[0] += b;
}

template <typename T>
void MultiplyPolynomial(std::vector<T> res, const std::vector<T> &a,
                        const std::vector<T> &b) {
  res.resize(a.size() + b.size() - 1);
  std::fill(res.begin(), res.end(), 0);
  for (size_t i = 0; i < a.size(); ++i) {
    for (size_t j = 0; j < b.size(); ++j) {
      res[i + j] += a[i] * b[j];
    }
  }
}

template <typename T, typename C>
void MultiplyPolynomial(std::vector<T> res, const std::vector<T> &a,
                        const C &b) {
  res.resize(a.size());
  std::fill(res.begin(), res.end(), 0);
  for (size_t i = 0; i < a.size(); ++i) {
    res[i] = a[i] * b;
  }
}

}  // namespace

namespace cheddar {

// ------------------------ AXYPBZ ------------------------

template <typename word>
void AXYPBZ<word>::AssertSameLevelAndScale(ConstContextPtr<word> context,
                                           const Ct &ct, int level,
                                           double scale) const {
  AssertTrue(ct.GetNP() == context->param_.LevelToNP(level), "Level mismatch");
  context->AssertSameScale(ct, scale);
}

template <typename word>
AXYPBZ<word>::AXYPBZ(ConstContextPtr<word> context, int a, double b,
                     int x_level, double x_scale, int y_level, double y_scale,
                     int z_level, double z_scale)
    : has_z_{true},
      x_level_{x_level},
      x_scale_{x_scale},
      y_level_{y_level},
      y_scale_{y_scale},
      z_level_{z_level},
      z_scale_{z_scale} {
  double working_scale = x_scale * y_scale;
  double b_scale = working_scale / z_scale;
  int working_level = Min(x_level, y_level, z_level);
  AssertTrue(working_level >= 1, "AXYPBZ: Invalid levels");
  AssertTrue(context->IsMultUnsafeCompatible(x_level, working_level) &&
                 context->IsMultUnsafeCompatible(y_level, working_level),
             "AXYPBZ: Invalid levels");
  if (b == 0.0) {
    has_b_ = false;
  } else {
    has_b_ = true;
    AssertTrue(context->IsMultUnsafeCompatible(z_level, working_level),
               "AXYPBZ: Invalid levels");
    context->encoder_.EncodeConstant(b_, working_level, b_scale, b);
  }

  AssertTrue(a != 0, "AXYPBZ: a should not be 0");
  if (a == 1) {
    has_a_ = false;
  } else {
    has_a_ = true;
    context->encoder_.EncodeConstant(a_, working_level, 1.0, a);
  }

  final_level_ = working_level - 1;
  final_scale_ =
      working_scale / context->param_.GetRescalePrimeProd(working_level);
}

// a * x * y + b
template <typename word>
AXYPBZ<word>::AXYPBZ(ConstContextPtr<word> context, int a, double b,
                     int x_level, double x_scale, int y_level, double y_scale)
    : has_z_{false},
      x_level_{x_level},
      x_scale_{x_scale},
      y_level_{y_level},
      y_scale_{y_scale},
      z_level_{0},
      z_scale_{0} {
  double working_scale = x_scale * y_scale;
  int working_level = Min(x_level, y_level);
  AssertTrue(working_level >= 1, "AXYPBZ: Invalid levels");
  AssertTrue(context->IsMultUnsafeCompatible(x_level, working_level) &&
                 context->IsMultUnsafeCompatible(y_level, working_level),
             "AXYPBZ: Invalid levels");
  if (b == 0.0) {
    has_b_ = false;
  } else {
    has_b_ = true;
    context->encoder_.EncodeConstant(b_, working_level, working_scale, b);
  }

  AssertTrue(a != 0, "AXYPBZ: a should not be 0");
  if (a == 1) {
    has_a_ = false;
  } else {
    has_a_ = true;
    context->encoder_.EncodeConstant(a_, working_level, 1.0, a);
  }

  final_level_ = working_level - 1;
  final_scale_ =
      working_scale / context->param_.GetRescalePrimeProd(working_level);
}

template <typename word>
void AXYPBZ<word>::Evaluate(ConstContextPtr<word> context, Ct &res, const Ct &x,
                            const Ct &y, const Ct &z,
                            const Evk &mult_key) const {
  AssertTrue(has_z_, "Z should be provided for AXYPBZ with Z");
  AssertTrue(!x.HasRx() && !y.HasRx() && !z.HasRx(),
             "AXYPBZ: Relinearization required");
  AssertSameLevelAndScale(context, x, x_level_, x_scale_);
  AssertSameLevelAndScale(context, y, y_level_, y_scale_);
  AssertSameLevelAndScale(context, z, z_level_, z_scale_);

  Ct tmp1;
  if (has_a_) {  // a != 1
    context->MultUnsafe(tmp1, x, a_, final_level_ + 1);
    context->MultUnsafe(tmp1, tmp1, y, final_level_ + 1);
  } else {  // a == 1
    context->MultUnsafe(tmp1, x, y, final_level_ + 1);
  }

  if (has_b_) {
    std::vector<DvView<word>> dst{tmp1.BxView(), tmp1.AxView()};
    std::vector<DvConstView<word>> tmp1_view = {tmp1.BxConstView(),
                                                tmp1.AxConstView()};
    NPInfo np = tmp1.GetNP();
    NPInfo z_np = z.GetNP();
    int ter_diff = z_np.num_ter_ - np.num_ter_;
    AssertTrue(ter_diff >= 0, "AXYPBZ: Invalid levels");
    std::vector<DvConstView<word>> z_view = z.ConstViewVector(ter_diff);
    context->elem_handler_.CAccum(dst, np, {z_view, tmp1_view},
                                  {b_.ConstView()});
    tmp1.SetNumSlots(Max(tmp1.GetNumSlots(), z.GetNumSlots()));
  }  // else, b == 0.0
  context->RelinearizeRescale(res, tmp1, mult_key);

  res.SetScale(final_scale_);
}

template <typename word>
void AXYPBZ<word>::Evaluate(ConstContextPtr<word> context, Ct &res, const Ct &x,
                            const Ct &y, const Evk &mult_key) const {
  AssertTrue(!has_z_, "Z should not be provided for AXYPBZ without Z");
  AssertTrue(!x.HasRx() && !y.HasRx(), "AXYPBZ: Relinearization required");
  AssertSameLevelAndScale(context, x, x_level_, x_scale_);
  AssertSameLevelAndScale(context, y, y_level_, y_scale_);

  Ct tmp1;
  if (has_a_) {  // a != 1
    context->MultUnsafe(tmp1, x, a_, final_level_ + 1);
    context->MultUnsafe(tmp1, tmp1, y, final_level_ + 1);
  } else {  // a == 0
    context->MultUnsafe(tmp1, x, y, final_level_ + 1);
  }

  if (has_b_) {  // b != 0.0
    context->Add(tmp1, tmp1, b_);
  }  // else, b == 0.0
  context->RelinearizeRescale(res, tmp1, mult_key);

  res.SetScale(final_scale_);
}

template <typename word>
void AXYPBZ<word>::PrepareTensor(ConstContextPtr<word> context, Ct &tmp1,
                                 const Ct &x, const Ct &y, const Ct *z) const {
  AssertTrue(has_z_ == (z != nullptr), "AXYPBZ: Z presence mismatch");
  AssertTrue(!x.HasRx() && !y.HasRx() && (z == nullptr || !z->HasRx()),
             "AXYPBZ: Relinearization required");
  AssertSameLevelAndScale(context, x, x_level_, x_scale_);
  AssertSameLevelAndScale(context, y, y_level_, y_scale_);
  if (z != nullptr) AssertSameLevelAndScale(context, *z, z_level_, z_scale_);

  int working_level = static_cast<int>(final_level_) + 1;
  NPInfo np = context->param_.LevelToNP(working_level);
  tmp1.ModifyNP(np);
  tmp1.PrepareRx();
  int num_slots = Max(x.GetNumSlots(), y.GetNumSlots());
  if (z != nullptr && has_b_) num_slots = Max(num_slots, z->GetNumSlots());
  tmp1.SetNumSlots(num_slots);
  tmp1.SetScale(x_scale_ * y_scale_);

  int x_ter_diff = x.GetNP().num_ter_ - np.num_ter_;
  int y_ter_diff = y.GetNP().num_ter_ - np.num_ter_;
  std::vector<DvConstView<word>> x_view = x.ConstViewVector(x_ter_diff, true);
  std::vector<DvConstView<word>> y_view = y.ConstViewVector(y_ter_diff, true);
  std::vector<DvConstView<word>> z_view;
  if (z != nullptr && has_b_) {
    int z_ter_diff = z->GetNP().num_ter_ - np.num_ter_;
    z_view = z->ConstViewVector(z_ter_diff, true);
  }
  DvConstView<word> a_view =
      has_a_ ? a_.ConstView() : DvConstView<word>(nullptr, 0, 0);
  DvConstView<word> b_view =
      has_b_ ? b_.ConstView() : DvConstView<word>(nullptr, 0, 0);
  std::vector<DvView<word>> dst = tmp1.ViewVector();
  context->elem_handler_.TensorAXYPBZ(dst, np, x_view, y_view, z_view, a_view,
                                      b_view);
}

// --------------------------------------------------------

// ------------------------ BasisMap ------------------------

template <typename word>
BasisMap<word>::BasisMap(int input_level, double input_scale, EvalPolyType type,
                         bool chebyshev)
    : input_level_(input_level),
      input_scale_(input_scale),
      type_(type),
      chebyshev_(chebyshev) {}

template <typename word>
int BasisMap<word>::SplitBaseDegree(int base_degree) const {
  AssertTrue(base_degree >= 2,
             "Invalid base degree " + std::to_string(base_degree));
  int split = 0;
  if (IsPowOfTwo(base_degree)) {
    split = base_degree / 2;
  } else if (type_ == EvalPolyType::kEven || base_degree == 13) {
    split = 1 << Log2Floor(base_degree);
  } else {
    split = (1 << (Log2Floor(base_degree))) - 1;
  }
  return Max(split, base_degree - split);
}

template <typename word>
std::pair<int, double> BasisMap<word>::GetBaseLevelAndScale(
    int base_degree) const {
  AssertTrue(Exists(base_degree), "BasisMap: base_degree does not exist.");
  int level;
  double scale;
  if (base_degree == 1) {
    level = input_level_;
    scale = input_scale_;
  } else {
    level = basis_eval_.at(base_degree).final_level_;
    scale = basis_eval_.at(base_degree).final_scale_;
  }

  return {level, scale};
}

template <typename word>
bool BasisMap<word>::Exists(int base_degree) const {
  return (base_degree == 1) ||
         basis_eval_.find(base_degree) != basis_eval_.end();
}

template <typename word>
void BasisMap<word>::AddBase(ConstContextPtr<word> context, int base_degree) {
  AssertTrue(base_degree >= 2,
             "AddBase: base_degree should be at least 2, given " +
                 std::to_string(base_degree));
  int left_degree = SplitBaseDegree(base_degree);
  int right_degree = base_degree - left_degree;
  // left_degree >= right_degree always holds

  if (!Exists(left_degree)) {
    AddBase(context, left_degree);
  }
  if (!Exists(right_degree)) {
    AddBase(context, right_degree);
  }

  auto [left_level, left_scale] = GetBaseLevelAndScale(left_degree);
  auto [right_level, right_scale] = GetBaseLevelAndScale(right_degree);
  int working_level = Min(left_level, right_level);
  // Find the highest level that is compatible with the working level
  while (!context->IsMultUnsafeCompatible(left_level, working_level)) {
    left_level -= 1;
  }
  // Find the highest level that is compatible with the working level
  while (!context->IsMultUnsafeCompatible(right_level, working_level)) {
    right_level -= 1;
  }

  if (chebyshev_) {  // Chebyshev basis
    int sub_degree = left_degree - right_degree;
    if (sub_degree == 0) {
      basis_eval_.try_emplace(base_degree, context, 2, -1, left_level,
                              left_scale, right_level, right_scale);
    } else {
      if (!Exists(sub_degree)) {
        AddBase(context, sub_degree);
      }
      auto [sub_level, sub_scale] = GetBaseLevelAndScale(sub_degree);
      while (!context->IsMultUnsafeCompatible(sub_level, working_level)) {
        sub_level -= 1;
      }
      basis_eval_.try_emplace(base_degree, context, 2, -1, left_level,
                              left_scale, right_level, right_scale, sub_level,
                              sub_scale);
    }
  } else {  // Normal basis
    basis_eval_.try_emplace(base_degree, context, 1, 0, left_level, left_scale,
                            right_level, right_scale);
  }
}

template <typename word>
void BasisMap<word>::Evaluate(ConstContextPtr<word> context,
                              std::map<int, MLCt> &res,
                              const Evk &mult_key) const {
  std::vector<std::map<int, MLCt>> batch;
  batch.push_back(std::move(res));
  EvaluateBatch(context, batch, mult_key);
  res = std::move(batch[0]);
}

template <typename word>
void BasisMap<word>::EvaluateBatch(ConstContextPtr<word> context,
                                   std::vector<std::map<int, MLCt>> &res_batch,
                                   const Evk &mult_key) const {
  AssertTrue(!basis_eval_.empty() && !res_batch.empty(),
             "BasisMap: basis_eval_ or res_batch is empty.");
  int C = res_batch.size();
  std::vector<std::pair<int, const AXYPBZ<word> *>> entries;
  entries.reserve(basis_eval_.size());
  for (const auto &[base_degree, eval] : basis_eval_) {
    entries.emplace_back(base_degree, &eval);
  }

  size_t idx = 0;
  while (idx < entries.size()) {
    int wave_x_level = entries[idx].second->x_level_;
    std::set<int> wave_degrees;
    size_t wave_end = idx;
    while (wave_end < entries.size()) {
      int deg = entries[wave_end].first;
      const auto *ev = entries[wave_end].second;
      if (ev->x_level_ != wave_x_level) break;
      int left_deg = SplitBaseDegree(deg);
      int right_deg = deg - left_deg;
      int sub_deg = left_deg - right_deg;
      if (wave_degrees.count(left_deg) || wave_degrees.count(right_deg) ||
          (chebyshev_ && sub_deg != 0 && wave_degrees.count(sub_deg))) {
        break;
      }
      wave_degrees.insert(deg);
      ++wave_end;
    }

    size_t W = wave_end - idx;
    size_t total = static_cast<size_t>(C) * W;
    std::vector<Ct> tmps(total), new_bases(total);
    std::vector<Ct *> res_ptrs(total);
    std::vector<const Ct *> tmp_ptrs(total);

    for (int c = 0; c < C; ++c) {
      for (size_t w = 0; w < W; ++w) {
        size_t flat = static_cast<size_t>(c) * W + w;
        int base_degree = entries[idx + w].first;
        const auto &eval = *entries[idx + w].second;
        int left_degree = SplitBaseDegree(base_degree);
        int right_degree = base_degree - left_degree;
        int sub_degree = left_degree - right_degree;
        int left_level = eval.x_level_;
        int right_level = eval.y_level_;

        context->AddLowerLevelsUntil(res_batch[c].at(left_degree), left_level);
        context->AddLowerLevelsUntil(res_batch[c].at(right_degree), right_level);
        const Ct &left = res_batch[c].at(left_degree).AtLevel(left_level);
        const Ct &right = res_batch[c].at(right_degree).AtLevel(right_level);
        const Ct *sub_ptr = nullptr;
        if (chebyshev_ && sub_degree != 0) {
          int sub_level = eval.z_level_;
          context->AddLowerLevelsUntil(res_batch[c].at(sub_degree), sub_level);
          sub_ptr = &res_batch[c].at(sub_degree).AtLevel(sub_level);
        }
        eval.PrepareTensor(context, tmps[flat], left, right, sub_ptr);
        res_ptrs[flat] = &new_bases[flat];
        tmp_ptrs[flat] = &tmps[flat];
      }
    }
    context->RelinearizeRescaleBatch(res_ptrs, tmp_ptrs, mult_key);
    tmps.clear();
    for (int c = 0; c < C; ++c) {
      for (size_t w = 0; w < W; ++w) {
        size_t flat = static_cast<size_t>(c) * W + w;
        int base_degree = entries[idx + w].first;
        const auto &eval = *entries[idx + w].second;
        new_bases[flat].SetScale(eval.final_scale_);
        res_batch[c].try_emplace(base_degree, std::move(new_bases[flat]));
      }
    }
    idx = wave_end;
  }
}

template <typename word>
void BasisMap<word>::PlainEvaluate(std::map<int, double> &res) const {
  AssertTrue(!basis_eval_.empty(), "BasisMap: basis_eval_ is empty.");
  for (const auto &[base_degree, eval] : basis_eval_) {
    int left_degree = SplitBaseDegree(base_degree);
    int right_degree = base_degree - left_degree;
    int sub_degree = Abs(left_degree - right_degree);
    res.try_emplace(base_degree, 0);
    double left = res.at(left_degree);
    double right = res.at(right_degree);
    res[base_degree] = left * right * (chebyshev_ ? 2 : 1);
    if (chebyshev_) {
      double sub = res.at(sub_degree);
      res[base_degree] -= sub;
    }
  }
}

// ----------------------------------------------------------

// ------------------------ EvalPolyNode ------------------------

template <typename word>
EvalPolyNode<word>::EvalPolyNode(const std::vector<double> &coefficients,
                                 int level_margin, int baby_threshold,
                                 bool chebyshev, double zero_coeff_threshold)
    : coefficients_{coefficients} {
  int degree = coefficients.size() - 1;

  int level_consumed_for_leaf_evaluation = Log2Ceil(degree) + 1;
  // Leaf creation
  if (degree < baby_threshold &&
      level_consumed_for_leaf_evaluation <= level_margin) {
    // Place holder for leaf constants;
    for (int i = 0; i <= degree; i++) {
      if (Abs(coefficients[i]) > zero_coeff_threshold) {
        leaf_constants_.try_emplace(i);
      }
    }
    return;
  }

  // Recursive creation
  split_degree_ = 1 << Log2Floor(degree);

  std::vector<double> low_coefficients(coefficients.begin(),
                                       coefficients.begin() + split_degree_);
  std::vector<double> high_coefficients(coefficients.begin() + split_degree_,
                                        coefficients.end());
  if (chebyshev) {
    int high_size = high_coefficients.size();
    for (int i = 1; i < high_size; i++) {
      low_coefficients[split_degree_ - i] -= high_coefficients[i];
      high_coefficients[i] *= 2;
    }
  }
  RemoveZero(low_coefficients, zero_coeff_threshold);
  RemoveZero(high_coefficients, zero_coeff_threshold);
  AssertFalse(high_coefficients.empty(),
              "Something went wrong during EvalPolyNode creation");

  // It is possible that low_coefficients are just a constant or 0
  // It is possible that high_coefficients are just a constant (but not 0)
  is_low_zero_ = low_coefficients.empty();
  is_low_constant_ = (low_coefficients.size() <= 1);
  is_high_constant_ = (high_coefficients.size() == 1);

  if (!is_low_constant_) {
    low_ = std::make_shared<EvalPolyNode<word>>(low_coefficients, level_margin,
                                                baby_threshold, chebyshev,
                                                zero_coeff_threshold);
  }
  if (!is_high_constant_) {
    high_ = std::make_shared<EvalPolyNode<word>>(
        high_coefficients, level_margin - 1, baby_threshold, chebyshev,
        zero_coeff_threshold);
  }
}

template <typename word>
bool EvalPolyNode<word>::IsLeaf() const {
  return high_ == nullptr && (!is_high_constant_);
}

template <typename word>
void EvalPolyNode<word>::CheckRequiredBasis(
    std::set<int> &required_bases) const {
  for (auto it = leaf_constants_.begin(); it != leaf_constants_.end(); it++) {
    required_bases.insert(it->first);
  }
  if (low_ != nullptr) low_->CheckRequiredBasis(required_bases);
  if (high_ != nullptr) high_->CheckRequiredBasis(required_bases);
  required_bases.insert(split_degree_);
}

template <typename word>
void EvalPolyNode<word>::Compile(ConstContextPtr<word> context,
                                 const BasisMap<word> &basis_eval,
                                 int target_level, double target_scale,
                                 bool do_rescale /*= true*/) {
  target_level_ = target_level;
  do_rescale_ = do_rescale;
  // The order of compilation
  int working_level = target_level;
  double working_scale = target_scale;
  if (do_rescale) {
    working_level = target_level + 1;
    working_scale =
        target_scale * context->param_.GetRescalePrimeProd(working_level);
  }

  if (is_low_constant_ && (!is_low_zero_)) {
    context->encoder_.EncodeConstant(low_constant_, working_level,
                                     working_scale, coefficients_[0]);
  } else if (low_ != nullptr) {
    // Low parts target higher scale (lazy rescaling)
    low_->Compile(context, basis_eval, working_level, working_scale, false);
  }
  if (is_high_constant_ || high_ != nullptr) {
    auto [_, split_scale] = basis_eval.GetBaseLevelAndScale(split_degree_);
    double high_scale = working_scale / split_scale;
    if (is_high_constant_) {
      context->encoder_.EncodeConstant(high_constant_, working_level,
                                       high_scale,
                                       coefficients_[split_degree_]);
    } else {
      high_->Compile(context, basis_eval, working_level, high_scale, true);
    }
  }

  // prepare leaf constants
  for (auto &[base_degree, constant] : leaf_constants_) {
    double value = coefficients_[base_degree];
    double scale = working_scale;
    if (base_degree != 0) {
      auto [_, base_scale] = basis_eval.GetBaseLevelAndScale(base_degree);
      scale /= base_scale;
    }
    context->encoder_.EncodeConstant(constant, working_level, scale, value);
  }
}

template <typename word>
void EvalPolyNode<word>::Evaluate(ConstContextPtr<word> context, Ct &res,
                                  std::map<int, MLCt> &basis,
                                  const Evk &mult_key) const {
  if (IsLeaf()) {
    // Evaluate is not for in-place operations
    EvaluateLeaf(context, res, basis, mult_key, false);
  } else {
    EvaluateMiddleNode(context, res, basis, mult_key);
  }
}

template <typename word>
void EvalPolyNode<word>::EvaluateMiddleNode(ConstContextPtr<word> context,
                                            Ct &res, std::map<int, MLCt> &basis,
                                            const Evk &mult_key) const {
  Ct tmp;
  Ct *accum = &res;
  int working_level = target_level_;
  if (do_rescale_) {
    working_level += 1;
    accum = &tmp;
  }
  AssertTrue(high_ != nullptr || is_high_constant_,
             "This is not a middle node");
  MLCt &ml_split = basis.at(split_degree_);
  int ml_split_level = ml_split.GetMaxLevel();
  while (!context->IsMultUnsafeCompatible(ml_split_level, working_level)) {
    ml_split_level -= 1;
  }
  context->AddLowerLevelsUntil(ml_split, ml_split_level);
  const Ct &split = ml_split.AtLevel(ml_split_level);

  if (high_ != nullptr) {
    high_->Evaluate(context, *accum, basis, mult_key);
    context->MultUnsafe(*accum, *accum, split, working_level);
  } else if (is_high_constant_) {
    context->MultUnsafe(*accum, split, high_constant_, working_level);
  } else {
    Fail("Something went wrong during middle node evaluation");
  }

  if (low_ != nullptr) {
    if (low_->IsLeaf()) {
      // This will perform inplace mad addition to accum (optimized)
      low_->EvaluateLeaf(context, *accum, basis, mult_key, true);
    } else {
      Ct tmp2;
      low_->Evaluate(context, tmp2, basis, mult_key);
      context->Add(*accum, *accum, tmp2);
    }
  } else if (is_low_constant_ && (!is_low_zero_)) {
    context->Add(*accum, *accum, low_constant_);
  }
  if (do_rescale_) {
    if (accum->HasRx()) {
      context->RelinearizeRescale(res, *accum, mult_key);
    } else {
      context->Rescale(res, *accum);
    }
  }
}

template <typename word>
void EvalPolyNode<word>::EvaluateLeaf(ConstContextPtr<word> context, Ct &res,
                                      std::map<int, MLCt> &basis,
                                      const Evk &mult_key, bool inplace) const {
  AssertFalse(do_rescale_ && inplace,
              "Rescale and inplace EvaluateLeaf is not compatible");
  AssertTrue(IsLeaf() && !leaf_constants_.empty(),
             "This is not a leaf node or leaf constants are not available.");

  Ct tmp;
  Ct *accum = &res;
  int working_level = target_level_;
  if (do_rescale_) {
    accum = &tmp;
    working_level += 1;
  }

  NPInfo np = context->param_.LevelToNP(working_level);
  std::vector<std::vector<DvConstView<word>>> ct_srcs;
  std::vector<DvConstView<word>> const_srcs;

  double scale = 0;
  int num_slots = 0;
  bool zero_const = false;
  for (const auto &[base_degree, constant] : leaf_constants_) {
    if (base_degree == 0) {
      zero_const = true;
      if (scale == 0) {
        scale = constant.GetScale();
      }
      // do nothing
    } else {
      MLCt &ml_ct = basis.at(base_degree);
      int ml_ct_level = ml_ct.GetMaxLevel();
      while (!context->IsMultUnsafeCompatible(ml_ct_level, working_level)) {
        ml_ct_level -= 1;
      }
      context->AddLowerLevelsUntil(ml_ct, ml_ct_level);
      const Ct &ct = ml_ct.AtLevel(ml_ct_level);
      int ter_diff = ct.GetNP().num_ter_ - np.num_ter_;
      AssertTrue(ter_diff >= 0, "Leaf evaluation level mismatch");
      ct_srcs.push_back(ct.ConstViewVector(ter_diff));
      const_srcs.push_back(constant.ConstView());
      num_slots = Max(num_slots, ct.GetNumSlots());
      if (scale == 0) {
        scale = ct.GetScale() * constant.GetScale();
      } else {
        context->AssertSameScale(scale, ct.GetScale() * constant.GetScale());
      }
    }
  }

  if (inplace) {
    AssertTrue(accum->GetNP() == np, "Leaf evaluation level mismatch");
    context->AssertSameScale(scale, accum->GetScale());
    accum->SetNumSlots(Max(num_slots, accum->GetNumSlots()));
    ct_srcs.push_back(accum->ConstViewVector(0, true));
  } else {
    accum->RemoveRx();
    accum->ModifyNP(np);
    accum->SetScale(scale);
    accum->SetNumSlots(num_slots);
  }
  std::vector<DvView<word>> dst = accum->ViewVector(0, true);
  context->elem_handler_.CAccum(dst, np, ct_srcs, const_srcs);
  if (zero_const) {
    context->Add(*accum, *accum, leaf_constants_.at(0));
  }

  if (do_rescale_) {
    context->Rescale(res, *accum);
  }
}

template <typename word>
void EvalPolyNode<word>::EvaluateLeavesBatch(
    ConstContextPtr<word> context,
    const std::vector<const EvalPolyNode<word> *> &nodes,
    const std::vector<Ct *> &res,
    const std::vector<std::map<int, MLCt> *> &bases, bool inplace) {
  int B = nodes.size();
  AssertTrue(B > 0 && static_cast<int>(res.size()) == B &&
                 static_cast<int>(bases.size()) == B,
             "EvaluateLeavesBatch: size mismatch");
  bool do_rescale = nodes[0]->do_rescale_;
  AssertFalse(do_rescale && inplace,
              "Rescale and inplace EvaluateLeavesBatch is not compatible");
  int working_level = nodes[0]->target_level_ + (do_rescale ? 1 : 0);
  NPInfo np = context->param_.LevelToNP(working_level);

  std::vector<Ct> tmps(do_rescale ? B : 0);
  std::vector<Ct *> accum_ptrs(B);
  for (int b = 0; b < B; ++b) {
    AssertTrue(nodes[b]->IsLeaf() && !nodes[b]->leaf_constants_.empty() &&
                   nodes[b]->do_rescale_ == do_rescale &&
                   nodes[b]->target_level_ == nodes[0]->target_level_,
               "EvaluateLeavesBatch: incompatible leaf nodes");
    accum_ptrs[b] = do_rescale ? &tmps[b] : res[b];
  }

  std::vector<bool> visited(B, false);
  for (int start = 0; start < B; ++start) {
    if (visited[start]) continue;
    std::map<int, MLCt> *basis_ptr = bases[start];
    std::vector<int> group;
    std::set<int> degree_set;
    for (int b = start; b < B; ++b) {
      if (bases[b] == basis_ptr) {
        visited[b] = true;
        group.push_back(b);
        for (const auto &[deg, _] : nodes[b]->leaf_constants_) {
          if (deg > 0) degree_set.insert(deg);
        }
      }
    }
    std::vector<int> degrees(degree_set.begin(), degree_set.end());
    std::vector<std::vector<DvConstView<word>>> shared_ct_srcs;
    int max_slots = 0;
    for (int deg : degrees) {
      MLCt &ml_ct = basis_ptr->at(deg);
      int ml_ct_level = ml_ct.GetMaxLevel();
      while (!context->IsMultUnsafeCompatible(ml_ct_level, working_level)) {
        ml_ct_level -= 1;
      }
      context->AddLowerLevelsUntil(ml_ct, ml_ct_level);
      const Ct &ct = ml_ct.AtLevel(ml_ct_level);
      int ter_diff = ct.GetNP().num_ter_ - np.num_ter_;
      AssertTrue(ter_diff >= 0, "Leaf evaluation level mismatch");
      shared_ct_srcs.push_back(ct.ConstViewVector(ter_diff));
      max_slots = Max(max_slots, ct.GetNumSlots());
    }

    if (!degrees.empty()) {
      std::vector<std::vector<DvView<word>>> dst_batch;
      std::vector<std::vector<DvConstView<word>>> const_srcs_batch;
      std::vector<std::vector<DvConstView<word>>> init_ct_batch;
      std::vector<DvConstView<word>> c0_consts_batch;
      for (int b : group) {
        double scale = 0;
        for (const auto &[base_degree, constant] : nodes[b]->leaf_constants_) {
          if (base_degree == 0) {
            if (scale == 0) scale = constant.GetScale();
          } else {
            int ml_ct_level = basis_ptr->at(base_degree).GetMaxLevel();
            while (
                !context->IsMultUnsafeCompatible(ml_ct_level, working_level)) {
              ml_ct_level -= 1;
            }
            const Ct &ct_at = basis_ptr->at(base_degree).AtLevel(ml_ct_level);
            if (scale == 0) {
              scale = ct_at.GetScale() * constant.GetScale();
            } else {
              context->AssertSameScale(scale,
                                       ct_at.GetScale() * constant.GetScale());
            }
          }
        }
        Ct *accum = accum_ptrs[b];
        if (inplace) {
          AssertTrue(accum->GetNP() == np, "Leaf evaluation level mismatch");
          context->AssertSameScale(scale, accum->GetScale());
          accum->SetNumSlots(Max(max_slots, accum->GetNumSlots()));
          init_ct_batch.push_back(accum->ConstViewVector(0, true));
        } else {
          accum->RemoveRx();
          accum->ModifyNP(np);
          accum->SetScale(scale);
          accum->SetNumSlots(max_slots);
        }
        dst_batch.push_back(accum->ViewVector(0, true));
        std::vector<DvConstView<word>> row_consts;
        for (int deg : degrees) {
          auto it = nodes[b]->leaf_constants_.find(deg);
          if (it != nodes[b]->leaf_constants_.end()) {
            row_consts.push_back(it->second.ConstView());
          } else {
            row_consts.emplace_back(nullptr, 0, 0);
          }
        }
        const_srcs_batch.push_back(std::move(row_consts));
        auto it0 = nodes[b]->leaf_constants_.find(0);
        if (it0 != nodes[b]->leaf_constants_.end()) {
          c0_consts_batch.push_back(it0->second.ConstView());
        } else {
          c0_consts_batch.emplace_back(nullptr, 0, 0);
        }
      }
      context->elem_handler_.MultiCAccum(dst_batch, np, shared_ct_srcs,
                                         const_srcs_batch, init_ct_batch,
                                         c0_consts_batch);
    } else {
      for (int b : group) {
        if (nodes[b]->leaf_constants_.count(0)) {
          context->Add(*accum_ptrs[b], *accum_ptrs[b],
                       nodes[b]->leaf_constants_.at(0));
        }
      }
    }
  }

  if (do_rescale) {
    std::vector<const Ct *> const_accum_ptrs(B);
    for (int b = 0; b < B; ++b) const_accum_ptrs[b] = accum_ptrs[b];
    context->RescaleBatch(res, const_accum_ptrs);
  }
}

template <typename word>
void EvalPolyNode<word>::EvaluateBatch(
    ConstContextPtr<word> context,
    const std::vector<const EvalPolyNode<word> *> &nodes,
    const std::vector<Ct *> &res,
    const std::vector<std::map<int, MLCt> *> &bases, const Evk &mult_key,
    bool defer_root_moddown, std::vector<Ct> *deferred_accums,
    bool can_clear_bases) {
  int B = nodes.size();
  AssertTrue(B > 0 && static_cast<int>(res.size()) == B &&
                 static_cast<int>(bases.size()) == B,
             "EvaluateBatch: size mismatch");
  bool all_leaves = true;
  bool all_same_middle = true;
  for (int b = 0; b < B; ++b) {
    if (!nodes[b]->IsLeaf()) all_leaves = false;
    if (nodes[b]->IsLeaf() ||
        nodes[b]->split_degree_ != nodes[0]->split_degree_ ||
        nodes[b]->target_level_ != nodes[0]->target_level_ ||
        nodes[b]->do_rescale_ != nodes[0]->do_rescale_ ||
        (nodes[b]->high_ != nullptr) != (nodes[0]->high_ != nullptr) ||
        nodes[b]->is_high_constant_ != nodes[0]->is_high_constant_ ||
        (nodes[b]->low_ != nullptr) != (nodes[0]->low_ != nullptr) ||
        (nodes[b]->low_ != nullptr &&
         nodes[b]->low_->IsLeaf() != nodes[0]->low_->IsLeaf())) {
      all_same_middle = false;
    }
  }
  if (all_leaves) {
    AssertFalse(defer_root_moddown, "Cannot defer moddown on leaf batch");
    EvaluateLeavesBatch(context, nodes, res, bases, false);
    if (can_clear_bases) {
      for (auto *basis_ptr : bases) basis_ptr->clear();
    }
    return;
  }
  if (!all_same_middle) {
    AssertFalse(defer_root_moddown, "Cannot defer moddown on mixed batch");
    for (int b = 0; b < B; ++b) {
      nodes[b]->Evaluate(context, *res[b], *bases[b], mult_key);
    }
    if (can_clear_bases) {
      for (auto *basis_ptr : bases) basis_ptr->clear();
    }
    return;
  }

  bool do_rescale = nodes[0]->do_rescale_;
  int working_level = nodes[0]->target_level_ + (do_rescale ? 1 : 0);
  int split_degree = nodes[0]->split_degree_;
  std::vector<Ct> tmps(do_rescale ? B : 0);
  std::vector<Ct *> accum_ptrs(B);
  for (int b = 0; b < B; ++b) {
    accum_ptrs[b] = do_rescale ? &tmps[b] : res[b];
  }

  if (nodes[0]->high_ != nullptr) {
    std::vector<const EvalPolyNode<word> *> high_nodes(B);
    for (int b = 0; b < B; ++b) high_nodes[b] = nodes[b]->high_.get();
    EvaluateBatch(context, high_nodes, accum_ptrs, bases, mult_key, false,
                  nullptr, false);

    MLCt &ml_split = bases[0]->at(split_degree);
    int ml_split_level = ml_split.GetMaxLevel();
    while (!context->IsMultUnsafeCompatible(ml_split_level, working_level)) {
      ml_split_level -= 1;
    }
    context->AddLowerLevelsUntil(ml_split, ml_split_level);
    const Ct &split = ml_split.AtLevel(ml_split_level);

    bool all_same_basis = true;
    for (int b = 1; b < B; ++b) {
      if (bases[b] != bases[0]) {
        all_same_basis = false;
        break;
      }
    }

    if (all_same_basis) {
      std::vector<const Ct *> const_accum_ptrs(B);
      for (int b = 0; b < B; ++b) const_accum_ptrs[b] = accum_ptrs[b];
      context->MultUnsafeBatchSharedY(accum_ptrs, const_accum_ptrs, split,
                                      working_level);
    } else {
      for (int b = 0; b < B; ++b) {
        MLCt &b_ml_split = bases[b]->at(split_degree);
        int b_split_level = b_ml_split.GetMaxLevel();
        while (!context->IsMultUnsafeCompatible(b_split_level, working_level)) {
          b_split_level -= 1;
        }
        context->AddLowerLevelsUntil(b_ml_split, b_split_level);
        context->MultUnsafe(*accum_ptrs[b], *accum_ptrs[b],
                            b_ml_split.AtLevel(b_split_level), working_level);
      }
    }
  } else if (nodes[0]->is_high_constant_) {
    for (int b = 0; b < B; ++b) {
      MLCt &ml_split = bases[b]->at(split_degree);
      int ml_split_level = ml_split.GetMaxLevel();
      while (!context->IsMultUnsafeCompatible(ml_split_level, working_level)) {
        ml_split_level -= 1;
      }
      context->AddLowerLevelsUntil(ml_split, ml_split_level);
      const Ct &split = ml_split.AtLevel(ml_split_level);
      context->MultUnsafe(*accum_ptrs[b], split, nodes[b]->high_constant_,
                          working_level);
    }
  } else {
    Fail("Something went wrong during batched middle node evaluation");
  }
  if (can_clear_bases) {
    for (auto *basis_ptr : bases) basis_ptr->erase(split_degree);
  }

  if (nodes[0]->low_ != nullptr) {
    std::vector<const EvalPolyNode<word> *> low_nodes(B);
    for (int b = 0; b < B; ++b) low_nodes[b] = nodes[b]->low_.get();
    if (nodes[0]->low_->IsLeaf()) {
      EvaluateLeavesBatch(context, low_nodes, accum_ptrs, bases, true);
      if (can_clear_bases) {
        for (auto *basis_ptr : bases) basis_ptr->clear();
      }
    } else {
      std::vector<Ct> tmp2s(B);
      std::vector<Ct *> tmp2_ptrs(B);
      for (int b = 0; b < B; ++b) tmp2_ptrs[b] = &tmp2s[b];
      EvaluateBatch(context, low_nodes, tmp2_ptrs, bases, mult_key, false,
                    nullptr, can_clear_bases);
      for (int b = 0; b < B; ++b) {
        context->Add(*accum_ptrs[b], *accum_ptrs[b], tmp2s[b]);
        tmp2s[b] = Ct();
      }
    }
  } else {
    if (can_clear_bases) {
      for (auto *basis_ptr : bases) basis_ptr->clear();
    }
    for (int b = 0; b < B; ++b) {
      if (nodes[b]->is_low_constant_ && !nodes[b]->is_low_zero_) {
        context->Add(*accum_ptrs[b], *accum_ptrs[b], nodes[b]->low_constant_);
      }
    }
  }

  if (do_rescale) {
    if (accum_ptrs[0]->HasRx()) {
      std::vector<const Ct *> const_accum_ptrs(B);
      for (int b = 0; b < B; ++b) const_accum_ptrs[b] = accum_ptrs[b];
      if (defer_root_moddown && deferred_accums != nullptr) {
        context->MultKeyNoModDownBatch(*deferred_accums, const_accum_ptrs,
                                       mult_key);
      } else {
        context->RelinearizeRescaleBatch(res, const_accum_ptrs, mult_key);
      }
    } else {
      AssertFalse(defer_root_moddown, "Cannot defer moddown without Rx");
      std::vector<const Ct *> const_accum_ptrs(B);
      for (int b = 0; b < B; ++b) const_accum_ptrs[b] = accum_ptrs[b];
      context->RescaleBatch(res, const_accum_ptrs);
    }
  }
}

template <typename word>
double EvalPolyNode<word>::PlainEvaluate(std::map<int, double> &basis) const {
  if (IsLeaf()) {
    double accum = 0;
    for (const auto &[base_degree, constant] : leaf_constants_) {
      accum += basis[base_degree] * coefficients_[base_degree];
    }
    return accum;
  } else {
    double split = basis[split_degree_];
    double high_value = (is_high_constant_) ? coefficients_[split_degree_]
                                            : high_->PlainEvaluate(basis);
    double low_value = 0;
    if (!is_low_zero_)
      low_value =
          (is_low_constant_) ? coefficients_[0] : low_->PlainEvaluate(basis);
    return low_value + high_value * split;
  }
}

// --------------------------------------------------------------

// ------------------------ EvalPoly ------------------------

template <typename word>
EvalPoly<word>::EvalPoly(const std::vector<double> &coefficients,
                         int input_level, double input_scale,
                         double target_scale, bool chebyshev,
                         double zero_coeff_threshold)
    : coefficients_{coefficients},
      zero_coeff_threshold_{zero_coeff_threshold},
      type_{DetermineType()},
      chebyshev_{chebyshev},
      input_level_{input_level},
      input_scale_{input_scale},
      target_scale_{target_scale},
      basis_map_(input_level, input_scale, type_, chebyshev) {}

template <typename word>
EvalPolyType EvalPoly<word>::DetermineType() {
  RemoveZero(coefficients_, zero_coeff_threshold_);
  AssertTrue(GetPolyDegree() >= 2,
             "Use EvalPoly for at least >= 2 degree poly.");
  int degree = GetPolyDegree();
  bool odd_flag = true;
  bool even_flag = true;
  for (int i = 0; i <= degree; i += 2) {
    // if there is a non-zero even term, it is not odd
    if (coefficients_[i] != 0) {
      odd_flag = false;
    }
  }
  for (int i = 1; i <= degree; i += 2) {
    // if there is a non-zero odd term, it is not even
    if (coefficients_[i] != 0) {
      even_flag = false;
    }
  }
  AssertTrue(!(even_flag && odd_flag), "Invalid polynomial type.");
  EvalPolyType type;
  if (even_flag) {
    type = EvalPolyType::kEven;
  } else if (odd_flag) {
    type = EvalPolyType::kOdd;
  } else {
    type = EvalPolyType::kNormal;
  }
  return type;
}

template <typename word>
int EvalPoly<word>::GetPolyDegree() const {
  return coefficients_.size() - 1;
}

template <typename word>
void EvalPoly<word>::ConvertToChebyshevBasis() {
  if (chebyshev_) return;
  PreparePlainChebyshevBasis();

  std::vector<double> old_coefficients = std::move(coefficients_);
  std::vector<double> new_coefficients(GetPolyDegree() + 1, 0);
  for (int i = 0; i <= GetPolyDegree(); i++) {
    auto &cheby_base = plain_chebyshev_basis_[i];
    double quotient = old_coefficients[i] / cheby_base.back();
    new_coefficients[i] = quotient;

    std::vector<double> temp;
    MultiplyPolynomial(temp, cheby_base, quotient);
    SubPolynomial(old_coefficients, old_coefficients, temp);
  }
  coefficients_ = std::move(new_coefficients);
  RemoveZero(coefficients_, zero_coeff_threshold_);
  type_ = DetermineType();
  chebyshev_ = true;
}

template <typename word>
void EvalPoly<word>::ConvertToNormalBasis() {
  if (!chebyshev_) return;
  PreparePlainChebyshevBasis();
  std::vector<double> new_coefficients(GetPolyDegree() + 1, 0);
  for (int i = 0; i <= GetPolyDegree(); i++) {
    std::vector<double> temp;
    MultiplyPolynomial(temp, plain_chebyshev_basis_[i], coefficients_[i]);
    AddPolynomial(new_coefficients, new_coefficients, temp);
  }
  coefficients_ = std::move(new_coefficients);
  RemoveZero(coefficients_, zero_coeff_threshold_);
  type_ = DetermineType();
  chebyshev_ = false;
}

template <typename word>
void EvalPoly<word>::PreparePlainChebyshevBasis() {
  while (plain_chebyshev_basis_.size() < coefficients_.size()) {
    int new_degree = plain_chebyshev_basis_.size();
    plain_chebyshev_basis_.emplace_back(new_degree + 1);
    if (new_degree == 0) {
      plain_chebyshev_basis_[new_degree][0] = 1;
    } else if (new_degree == 1) {
      plain_chebyshev_basis_[new_degree][0] = 0;
      plain_chebyshev_basis_[new_degree][1] = 1;
    } else {
      // T_n(x) = 2xT_(n-1)(x) - T_(n-2)(x)
      MultiplyPolynomial(plain_chebyshev_basis_[new_degree],
                         plain_chebyshev_basis_[new_degree - 1],
                         plain_chebyshev_basis_[1]);
      MultiplyPolynomial(plain_chebyshev_basis_[new_degree],
                         plain_chebyshev_basis_[new_degree], 2);
      SubPolynomial(plain_chebyshev_basis_[new_degree],
                    plain_chebyshev_basis_[new_degree],
                    plain_chebyshev_basis_[new_degree - 2]);
    }
  }
}

template <typename word>
void EvalPoly<word>::Compile(ConstContextPtr<word> context) {
  // Construct main evaluation tree
  int level_consumption = Log2Ceil(GetPolyDegree() + 1);
  int baby_threshold = 1 << DivCeil(level_consumption, 2);

  tree_root_ = std::make_shared<EvalPolyNode<word>>(
      coefficients_, level_consumption, baby_threshold, chebyshev_,
      zero_coeff_threshold_);

  // Construct basis evaluation sequences
  std::set<int> required_base_degrees;
  tree_root_->CheckRequiredBasis(required_base_degrees);

  for (int deg : required_base_degrees) {
    if (deg == 0 || deg == 1) continue;
    basis_map_.AddBase(context, deg);
  }

  // Actual compilation of the tree
  int target_level = input_level_ - level_consumption;
  tree_root_->Compile(context, basis_map_, target_level, target_scale_);
}

template <typename word>
void EvalPoly<word>::Evaluate(ConstContextPtr<word> context, Ct &res,
                              const Ct &input, const Evk &mult_key) const {
  AssertTrue(tree_root_ != nullptr, "EvalPoly: not compiled.");
  NPInfo np = input.GetNP();
  AssertTrue(context->param_.NPToLevel(np) == input_level_,
             "EvalPoly: input level does not match the compiled level.");
  AssertTrue(np.num_aux_ == 0, "ModDown required before EvalPoly evaluation");
  AssertFalse(input.HasRx(),
              "Relinearization required before EvalPoly evaluation");
  context->AssertSameScale(input, input_scale_);

  std::map<int, MLCt> basis;
  // To prevent problems in in-place operations and also to simplify the code
  Ct input_tmp;
  context->Copy(input_tmp, input);
  basis.try_emplace(1, std::move(input_tmp));

  basis_map_.Evaluate(context, basis, mult_key);
  tree_root_->Evaluate(context, res, basis, mult_key);
  // To avoid double calculation errors, manually set target scale
  context->AssertSameScale(res, target_scale_);
  res.SetScale(target_scale_);
}

template <typename word>
double EvalPoly<word>::PlainEvaluate(double input) const {
  AssertTrue(tree_root_ != nullptr, "EvalPoly: not compiled.");
  std::map<int, double> basis;
  basis.try_emplace(0, 1);
  basis.try_emplace(1, input);
  basis_map_.PlainEvaluate(basis);
  return tree_root_->PlainEvaluate(basis);
}

template class AXYPBZ<uint32_t>;
template class AXYPBZ<uint64_t>;

template class BasisMap<uint32_t>;
template class BasisMap<uint64_t>;

template class EvalPolyNode<uint32_t>;
template class EvalPolyNode<uint64_t>;

template class EvalPoly<uint32_t>;
template class EvalPoly<uint64_t>;

template <typename word>
EvalPolyBatch<word>::EvalPolyBatch(
    const std::vector<std::vector<double>> &coefficients, int input_level,
    double input_scale, double target_scale, bool chebyshev, int baby_step)
    : coefficients_(coefficients), input_level_(input_level),
      input_scale_(input_scale), target_scale_(target_scale),
      chebyshev_(chebyshev), baby_step_(baby_step) {
  AssertTrue(!coefficients.empty() && coefficients.front().size() >= 3,
             "EvalPolyBatch requires nontrivial polynomials");
  const auto size = coefficients.front().size();
  for (const auto &polynomial : coefficients) {
    AssertTrue(polynomial.size() == size && polynomial.back() != 0,
               "EvalPolyBatch requires the same actual degree");
    for (double coefficient : polynomial)
      AssertTrue(std::isfinite(coefficient), "Nonfinite polynomial coefficient");
  }
  depth_ = Log2Ceil(int(size));
  AssertTrue(baby_step == 0 ||
                 (baby_step >= 2 && (baby_step & (baby_step - 1)) == 0 &&
                  baby_step <= (1 << (depth_ - 1))),
             "Polynomial baby step must be a power of two within its depth");
  AssertTrue(input_level >= depth_, "Insufficient modulus for polynomial batch");
}

template <typename word>
void EvalPolyBatch<word>::Compile(ConstContextPtr<word> context) {
  AssertTrue(roots_.empty(), "EvalPolyBatch already compiled");
  bool odd = true, even = true;
  for (const auto &polynomial : coefficients_) {
    for (size_t index = 0; index < polynomial.size(); ++index) {
      if (polynomial[index] != 0) {
        if (index % 2) even = false;
        else odd = false;
      }
    }
  }
  const auto type = odd ? EvalPolyType::kOdd :
                    even ? EvalPolyType::kEven : EvalPolyType::kNormal;
  basis_ = std::make_unique<BasisMap<word>>(input_level_, input_scale_,
                                           type, chebyshev_);
  std::set<int> required;
  const int baby = baby_step_ ? baby_step_ : 1 << DivCeil(depth_, 2);
  for (const auto &polynomial : coefficients_) {
    roots_.push_back(std::make_unique<EvalPolyNode<word>>(
        polynomial, depth_, baby, chebyshev_,
        std::numeric_limits<double>::denorm_min()));
    roots_.back()->CheckRequiredBasis(required);
  }
  for (int degree : required)
    if (degree > 1) basis_->AddBase(context, degree);
  required_bases_ = std::move(required);
  for (auto &root : roots_)
    root->Compile(context, *basis_, input_level_ - depth_, target_scale_);
}

template <typename word>
void EvalPolyBatch<word>::Evaluate(
    ConstContextPtr<word> context, std::vector<Ciphertext<word>> &results,
    const Ciphertext<word> &input, const EvaluationKey<word> &mult_key,
    const Observer &observe) const {
  AssertTrue(!roots_.empty() && input.GetNP() ==
                 context->param_.LevelToNP(input_level_) && !input.HasRx(),
             "Invalid polynomial batch input state");
  context->AssertSameScale(input, input_scale_);
  Ciphertext<word> copy;
  context->Copy(copy, input);
  std::map<int, MultiLevelCiphertext<word>> powers;
  powers.try_emplace(1, std::move(copy));
  basis_->Evaluate(context, powers, mult_key);
  for (auto it = powers.begin(); it != powers.end();) {
    if (required_bases_.count(it->first) == 0) {
      it = powers.erase(it);
    } else {
      int max_level = it->second.GetMaxLevel();
      if (it->second.GetMinLevel() < max_level &&
          context->IsMultUnsafeCompatible(max_level, input_level_ - depth_)) {
        Ciphertext<word> top = std::move(it->second.AtLevel(max_level));
        it->second = MultiLevelCiphertext<word>(std::move(top));
      }
      ++it;
    }
  }
  if (observe) observe("basis", -1, input);
  std::vector<Ciphertext<word>> output(roots_.size());
  std::vector<const EvalPolyNode<word> *> root_ptrs(roots_.size());
  std::vector<Ciphertext<word> *> out_ptrs(roots_.size());
  std::vector<std::map<int, MultiLevelCiphertext<word>> *> tree_bases(
      roots_.size(), &powers);
  for (size_t index = 0; index < roots_.size(); ++index) {
    root_ptrs[index] = roots_[index].get();
    out_ptrs[index] = &output[index];
  }
  EvalPolyNode<word>::EvaluateBatch(context, root_ptrs, out_ptrs, tree_bases,
                                    mult_key, false, nullptr, true);
  powers.clear();
  for (size_t index = 0; index < roots_.size(); ++index) {
    AssertTrue(output[index].GetNP() ==
                   context->param_.LevelToNP(input_level_ - depth_),
               "Polynomial batch output level differs from compiled depth");
    context->AssertSameScale(output[index], target_scale_);
    output[index].SetScale(target_scale_);
    if (observe) observe("tree", int(index), output[index]);
  }
  results = std::move(output);
}

template <typename word>
void EvalPolyBatch<word>::EvaluateBatch(
    ConstContextPtr<word> context,
    std::vector<std::vector<Ciphertext<word>>> &results_batch,
    const std::vector<const Ciphertext<word> *> &inputs,
    const EvaluationKey<word> &mult_key) const {
  if (inputs.empty()) {
    results_batch.clear();
    return;
  }
  if (inputs.size() == 1) {
    results_batch.resize(1);
    Evaluate(context, results_batch[0], *inputs[0], mult_key);
    return;
  }
  const size_t num_inputs = inputs.size();
  std::vector<std::map<int, MultiLevelCiphertext<word>>> powers_batch(num_inputs);
  for (size_t b = 0; b < num_inputs; ++b) {
    const auto &input = *inputs[b];
    AssertTrue(!roots_.empty() &&
                   input.GetNP() == context->param_.LevelToNP(input_level_) &&
                   !input.HasRx(),
               "Invalid polynomial batch input state");
    context->AssertSameScale(input, input_scale_);
    Ciphertext<word> copy;
    context->Copy(copy, input);
    powers_batch[b].try_emplace(1, std::move(copy));
  }
  basis_->EvaluateBatch(context, powers_batch, mult_key);
  for (size_t b = 0; b < num_inputs; ++b) {
    for (auto it = powers_batch[b].begin(); it != powers_batch[b].end();) {
      if (required_bases_.count(it->first) == 0) {
        it = powers_batch[b].erase(it);
      } else {
        int max_level = it->second.GetMaxLevel();
        if (it->second.GetMinLevel() < max_level &&
            context->IsMultUnsafeCompatible(max_level, input_level_ - depth_)) {
          Ciphertext<word> top = std::move(it->second.AtLevel(max_level));
          it->second = MultiLevelCiphertext<word>(std::move(top));
        }
        ++it;
      }
    }
  }

  std::vector<std::vector<Ciphertext<word>>> output_batch(num_inputs);
  const size_t total_trees = num_inputs * roots_.size();
  std::vector<const EvalPolyNode<word> *> root_ptrs;
  std::vector<Ciphertext<word> *> out_ptrs;
  std::vector<std::map<int, MultiLevelCiphertext<word>> *> tree_bases;
  root_ptrs.reserve(total_trees);
  out_ptrs.reserve(total_trees);
  tree_bases.reserve(total_trees);
  for (size_t b = 0; b < num_inputs; ++b) {
    output_batch[b].resize(roots_.size());
    for (size_t index = 0; index < roots_.size(); ++index) {
      root_ptrs.push_back(roots_[index].get());
      out_ptrs.push_back(&output_batch[b][index]);
      tree_bases.push_back(&powers_batch[b]);
    }
  }
  EvalPolyNode<word>::EvaluateBatch(context, root_ptrs, out_ptrs, tree_bases,
                                    mult_key, false, nullptr, true);
  for (size_t b = 0; b < num_inputs; ++b) {
    for (size_t index = 0; index < roots_.size(); ++index) {
      AssertTrue(output_batch[b][index].GetNP() ==
                     context->param_.LevelToNP(input_level_ - depth_),
                 "Polynomial batch output level differs from compiled depth");
      context->AssertSameScale(output_batch[b][index], target_scale_);
      output_batch[b][index].SetScale(target_scale_);
    }
  }
  results_batch = std::move(output_batch);
}

template class EvalPolyBatch<uint32_t>;
template class EvalPolyBatch<uint64_t>;

}  // namespace cheddar
