#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/Context.h"
#include "core/Type.h"
#include "extension/StripedMatrix.h"

namespace cheddar {

struct MEBootStagePlan {
  int stage_index;
  int width;
  int stride;
  std::vector<int> supports;  // Sorted non-zero diagonal offsets modulo num_slots
  StripedMatrix matrix;       // Diagonal matrix of complex weights
  int bs;                     // Baby step size
  int gs;                     // Giant step count
};

/**
 * @brief MEBootMatrixPlanner computes the four-stage [m1, m2, m3, m4]
 * factorization and folded BSGS diagonal matrices for the CtS linear transform.
 */
class MEBootMatrixPlanner {
 public:
  /**
   * @brief Computes theoretical diagonal supports for each stage given slot
   * count and stage widths.
   *
   * @param log_slots log2 of slot count (e.g. 15 for 32768 slots)
   * @param widths width of each stage (e.g. {4, 4, 3, 4})
   * @return std::vector<std::vector<int>> sorted diagonal offsets per stage
   */
  static std::vector<std::vector<int>> ComputeSupports(
      int log_slots, const std::vector<int> &widths);

  /**
   * @brief Generates radix-2 elementary IFFT stage matrices.
   *
   * @tparam word uint32_t or uint64_t
   * @param context ConstContextPtr to Cheddar context
   * @param num_slots slot count
   * @return std::vector<StripedMatrix> ordered elementary IFFT stages
   */
  template <typename word>
  static std::vector<StripedMatrix> GenerateElementaryIFFTStages(
      ConstContextPtr<word> context, int num_slots);

  template <typename word>
  static std::vector<StripedMatrix> GenerateElementaryIFFTStages(
      ContextPtr<word> context, int num_slots) {
    return GenerateElementaryIFFTStages(ConstContextPtr<word>(context),
                                        num_slots);
  }

  /**
   * @brief Plans the complete multi-stage CtS linear transformation.
   *
   * @tparam word uint32_t or uint64_t
   * @param context ConstContextPtr to Cheddar context
   * @param num_slots slot count (must be power of two)
   * @param widths widths of each stage (defaults to {4, 4, 3, 4})
   * @param dense_bases dense BSGS bases for M3 and M4 (defaults to {64, 8})
   * @param scaling_factor overall scaling factor to distribute across stages
   * @return std::vector<MEBootStagePlan> plans for all stages
   */
  template <typename word>
  static std::vector<MEBootStagePlan> PlanCtS(
      ConstContextPtr<word> context, int num_slots,
      const std::vector<int> &widths = {4, 4, 3, 4},
      const std::vector<int> &dense_bases = {64, 8},
      double scaling_factor = 1.0);

  template <typename word>
  static std::vector<MEBootStagePlan> PlanCtS(
      ContextPtr<word> context, int num_slots,
      const std::vector<int> &widths = {4, 4, 3, 4},
      const std::vector<int> &dense_bases = {64, 8},
      double scaling_factor = 1.0) {
    return PlanCtS(ConstContextPtr<word>(context), num_slots, widths,
                   dense_bases, scaling_factor);
  }
};

}  // namespace cheddar
