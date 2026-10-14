#pragma once

#include <vector>

#include "core/Container.h"
#include "core/DeviceVector.h"
#include "core/ElementWise.h"
#include "core/NTT.h"
#include "core/Parameter.h"

namespace cheddar {

template <typename word>
class ModSwitchHandler {
 private:
  using Dv = DeviceVector<word>;
  using Ct = Ciphertext<word>;

  // The order matters here
  const int level_;
  const int num_aux_;
  // Padding offset within the key basis, excluding OverModRaise primes.
  const int prime_offset_;
  const int padded_num_q_;
  const int beta_;

  const Parameter<word> &param_;
  const ElementWiseHandler<word> &elem_handler_;
  const NTTHandler<word> &ntt_handler_;

 public:
  ModSwitchHandler(const Parameter<word> &param, int level,
                   const ElementWiseHandler<word> &elem_handler,
                   const NTTHandler<word> &ntt_handler);

  // disable copying
  ModSwitchHandler(const ModSwitchHandler &) = delete;
  ModSwitchHandler &operator=(const ModSwitchHandler &) = delete;

  // for forwarding purposes
  ModSwitchHandler(ModSwitchHandler &&) = default;

  int GetLevel() const { return level_; }
  int GetNumAux() const { return num_aux_; }
  int GetPrimeOffset() const { return prime_offset_; }
  int GetPaddedNumQ() const { return padded_num_q_; }
  int GetBeta() const { return beta_; }
  bool IsChunkActive(int chunk_idx) const;

  void AllocateModUpBuffers(std::vector<Dv> &buffers,
                            std::vector<DvView<word>> &views) const;

  void PseudoModUp(DvView<word> &dst, const DvConstView<word> &src,
                   const DvConstView<word> &p_prod) const;
  void ModUp(std::vector<DvView<word>> &dst,
             const DvConstView<word> &src) const;
  void ModUp(std::vector<Dv> &dst_buffers,
             const DvConstView<word> &src) const;

  void ModDown(DvView<word> &dst, const DvConstView<word> &src) const;
  void ModDown(Ct &dst, const Ct &src) const;

  void Rescale(DvView<word> &dst, const DvConstView<word> &src) const;
  void Rescale(Ct &dst, const Ct &src) const;

  void ModDownAndRescale(DvView<word> &dst, const DvConstView<word> &src) const;
  void ModDownAndRescale(Ct &dst, const Ct &src) const;
  void EnableCenteredMergedRescale();
  void EnableCenteredModUp();

 private:
  // ModUp constants
  Dv mod_up1_;
  std::vector<DeviceVector<make_signed_t<word>>> mod_up2_;

  // ModDown constants
  Dv mod_down1_;
  DeviceVector<make_signed_t<word>> mod_down2_;
  Dv inv_prime_prod_;

  // Rescale constants
  int rescale_pad_start_;
  int rescale_pad_end_;
  int rescale_restore_start_;
  int rescale_restore_end_;

  Dv rescale1_;
  DeviceVector<make_signed_t<word>> rescale2_;
  Dv rescale_inv_prime_prod_;
  Dv rescale_padding_;

  // ModDownAndRescale constants
  Dv mod_down_rescale1_;
  DeviceVector<make_signed_t<word>> mod_down_rescale2_;
  Dv mod_down_rescale_inv_prime_prod_;
  Dv mod_down_rescale_padding_;
  DeviceVector<uint64_t> centered_reciprocals_;
  DeviceVector<uint64_t> centered_product_, centered_cofactors_;
  Dv centered_product_mod_q_;
  struct CenteredConstants {
    DeviceVector<uint64_t> reciprocals, product, cofactors;
    Dv product_mod_q;
  };
  std::vector<CenteredConstants> centered_mod_up_;
  void PopulateCenteredConstants(
      const std::vector<word> &source, const std::vector<word> &target,
      DeviceVector<uint64_t> &reciprocals, DeviceVector<uint64_t> &product,
      DeviceVector<uint64_t> &cofactors, Dv &product_mod_q);

  Dv entire_padding_;

  // heuristic CUDA kernel block number;
  static constexpr int block_dim_ = 256;
  static inline bool cm_populated_ = false;

  void PopulateModSwitchConstants(Dv &const1,
                                  DeviceVector<make_signed_t<word>> &const2,
                                  const std::vector<word> &src_primes,
                                  const std::vector<word> &dst_primes,
                                  int restore_start, int restore_end);
  void PopulateModDownEpilogueConstants(Dv &inv_p_prod, Dv &padding,
                                        const std::vector<word> &src_primes,
                                        const std::vector<word> &dst_primes,
                                        int restore_start, int restore_end);

  enum class ModDownType { ModDown, Rescale, ModDownAndRescale };
  void ModDownWorker(DvView<word> &dst, const DvConstView<word> &src,
                     ModDownType type) const;
};

}  // namespace cheddar
