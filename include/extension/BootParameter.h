#pragma once

#include <vector>

namespace cheddar {

/**
 * @brief A class for storing bootstrapping parameters.
 *
 */
struct BootParameter {
  /**
   * @brief Construct a new BootParameter object
   *
   * @param max_level the maximum level
   * @param num_cts_levels normal-chain consumption for CtS (MEBOOT fixes it to 2)
   * @param num_stc_levels the number of levels for SlotToCoeff (StC)
   * @param log_message_ratio approximately log2(q0 / scale to target at the
   * lowest level). Do not modify if you do not know what you are doing.
   * @param use_meboot_cts whether to use MEBoot CtS (default false)
   * @param num_double_angle validated 3 (K=16) or 5 (K=64) range preset
   */
  BootParameter(int max_level, int num_cts_levels, int num_stc_levels,
                int log_message_ratio = 5, bool use_meboot_cts = false,
                int num_double_angle = 3);

  const int max_level_;
  const int num_cts_levels_;
  const int num_prefix_levels_;
  const int num_stc_levels_;

  const int log_message_ratio_;
  const bool use_meboot_cts_;

  // The polynomial normalization and double-angle count are selected together.
  const std::vector<double> mod_coefficients_;
  const int num_double_angle_;
  const int initial_K_;

  /**
   * @brief Get the level consumption for EvalMod
   *
   * @return int the number of levels consumed by EvalMod
   */
  int GetNumEvalModLevels() const;

  /**
   * @brief Get the maximum level
   *
   * @return int the maximum level
   */
  int GetMaxLevel() const;

  /**
   * @brief Get the starting level for CoeffToSlot (CtS)
   *
   * @return int the starting level for CoeffToSlot (CtS)
   */
  int GetCtSStartLevel() const;

  /**
   * @brief Get the starting level for EvalMod
   *
   * @return int the starting level for EvalMod
   */
  int GetEvalModStartLevel() const;

  /**
   * @brief Get the starting level for SlotToCoeff (StC)
   *
   * @return int the starting level for SlotToCoeff (StC)
   */
  int GetStCStartLevel() const;

  /**
   * @brief Get the starting level for bootstrapping. Should be the same as
   * GetMaxLevel() and GetCtSStartLevel().
   *
   * @return int the starting level for bootstrapping
   */
  int GetStartLevel() const;

  /**
   * @brief Get the ending level for bootstrapping. Should be the same as
   * default_encryption_level_.
   *
   * @return int the ending level for bootstrapping
   */
  int GetEndLevel() const;
};

}  // namespace cheddar
