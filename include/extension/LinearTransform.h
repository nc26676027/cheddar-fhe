#pragma once

#include <complex>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

#include "common/Assert.h"
#include "core/Context.h"
#include "core/EvkMap.h"
#include "core/EvkRequest.h"
#include "extension/StripedMatrix.h"

namespace cheddar {

/**
 * @brief Deep homomorphic linear transformation evaluator combining diagonal
 * matrix BSGS partitioning, double-hoisted plaintext encoding, and baby-step /
 * giant-step GPU evaluation behind a unified seam.
 *
 * @tparam word uint32_t or uint64_t
 */
template <typename word>
class LinearTransform {
 private:
  using Dv = DeviceVector<word>;
  using Ct = Ciphertext<word>;
  using Pt = Plaintext<word>;
  using Evk = EvaluationKey<word>;
  using Complex = std::complex<double>;
  using Message = std::vector<Complex>;
  using PlainHoistMap = std::map<int, std::map<int, Message>>;

  std::weak_ptr<const Context<word>> context_;
  int pt_level_;
  double pt_scale_;

  int bs_;
  int gs_;
  int pre_rotation_;
  int additional_pt_rot_;
  int stride_;

  std::set<int> bs_indices_;
  std::vector<int> gs_indices_;
  std::map<int, std::map<int, Pt>> hoist_pt_map_;

  static constexpr int kernel_block_dim_ = 256;
  static inline bool cm_populated_ = false;
  static constexpr int max_log_beta_ = 4;
  static constexpr int max_log_bs_ = 7;

  ConstContextPtr<word> GetContext() const {
    auto ctx = context_.lock();
    AssertTrue(ctx != nullptr, "LinearTransform: Context has been destroyed");
    return ctx;
  }

  int DetermineStride(const StripedMatrix &matrix) const;
  PlainHoistMap ConstructPlainHoistMap(const StripedMatrix &matrix) const;
  PlainHoistMap ConstructDenseHoistMap(const StripedMatrix &matrix,
                                       int dense_base) const;
  void InitializeHoistPlaintexts(ConstContextPtr<word> context,
                                 const PlainHoistMap &hoist_map,
                                 bool suppress_bs_swap = false);
  void ExtractBSIndices(const PlainHoistMap &hoist_map);
  void CompilePlaintexts(ConstContextPtr<word> context,
                         const PlainHoistMap &hoist_map);
  std::pair<int, int> CheckStrideMinKS() const;

  void GSFusedPAccum(ConstContextPtr<word> context, std::map<int, Ct> &results,
                     const std::vector<int> &gs_indices,
                     const std::map<int, Ct> &bs) const;
  void BSFusedKeyMult(ConstContextPtr<word> context, std::map<int, Ct> &res,
                      std::vector<Dv> &a_modup, const Ct &a_orig,
                      const EvkMap<word> &keys, std::vector<int> &rotations,
                      const Dv &input_bx_pseudo_modup) const;

  void EvaluateSingleAccum(ConstContextPtr<word> context, Ct &res,
                           const std::map<int, Ct> &bs,
                           const std::map<int, Pt> &pt_map,
                           bool inplace = false) const;
  void EvaluateFinalModDown(ConstContextPtr<word> context, Ct &res,
                            Ct &final_accum, int input_num_slots,
                            double input_scale) const;
  void EvaluateMinKSBabyStep(ConstContextPtr<word> context,
                             std::map<int, Ct> &bs, const Ct &input,
                             const EvkMap<word> &evk_map) const;
  void EvaluateMinKSGiantStep(ConstContextPtr<word> context, Ct &res,
                              const std::map<int, Ct> &bs,
                              const EvkMap<word> &evk_map) const;
  void EvaluateGiantStepOptimized(ConstContextPtr<word> context, Ct &res,
                                  const std::map<int, Ct> &bs,
                                  const EvkMap<word> &evk_map) const;
  void EvaluateBabyStep(ConstContextPtr<word> context, std::map<int, Ct> &bs,
                        const Ct &input, const EvkMap<word> &evk_map,
                        bool min_ks = false) const;
  void EvaluateGiantStep(ConstContextPtr<word> context, Ct &res,
                         const std::map<int, Ct> &bs,
                         const EvkMap<word> &evk_map,
                         bool min_ks = false) const;

 public:
  static std::pair<int, int> DefaultBSGSSplit(int num_diag);

  /**
   * @brief Construct a LinearTransform with automatic BSGS partitioning.
   */
  LinearTransform(ConstContextPtr<word> context, const StripedMatrix &matrix,
                  int pt_level, double pt_scale);

  /**
   * @brief Construct a LinearTransform with explicit BSGS and rotation offsets.
   */
  LinearTransform(ConstContextPtr<word> context, const StripedMatrix &matrix,
                  int pt_level, double pt_scale, int bs, int gs,
                  int pre_rotation = 0, int additional_pt_rot = 0);

  /**
   * @brief Construct a LinearTransform using dense BSGS folding base.
   */
  LinearTransform(ConstContextPtr<word> context, const StripedMatrix &matrix,
                  int pt_level, double pt_scale, int dense_base);

  LinearTransform(const LinearTransform &) = delete;
  LinearTransform &operator=(const LinearTransform &) = delete;
  LinearTransform(LinearTransform &&) = default;
  LinearTransform &operator=(LinearTransform &&) = default;

  bool IsUsingBSGS() const;
  int GetBS() const;
  int GetGS() const;
  int GetPreRotationAmount() const;

  void AddRequiredRotations(EvkRequest &req, bool min_ks = false) const;

  void Evaluate(Ct &res, const Ct &input, const EvkMap<word> &evk_map,
                bool min_ks = false) const;

  void Evaluate(ConstContextPtr<word> context, Ct &res, const Ct &input,
                const EvkMap<word> &evk_map, bool min_ks = false) const;
};

}  // namespace cheddar