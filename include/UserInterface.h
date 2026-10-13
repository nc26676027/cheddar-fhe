#pragma once

/**
 * @brief The code inside this file is for test purposes and are NOT secure
 * implementations.
 *
 */

#include "Random.h"
#include "core/AKSKeyMap.h"
#include "core/Context.h"
#include "core/EvkMap.h"
#include "core/EvkRequest.h"

namespace cheddar {

class StripedMatrix;
template <typename word>
class MEBootCtS;

enum class SparseSecretLayout { kFullRing, kSubringPrefix };

/**
 * @brief This class provides a simple unoptimized client interface for CKKS.
 * The security of this class is not guaranteed and should not be used in
 * production. This class is intended for testing purposes only.
 *
 * @tparam word uint32_t or uint64_t
 */
template <typename word>
class UserInterface {
  using Dv = DeviceVector<word>;
  using Ct = Ciphertext<word>;
  using Pt = Plaintext<word>;
  using Evk = EvaluationKey<word>;

  static inline bool cm_populated_ = false;

 public:
  /**
   * @brief Construct a new UserInterface object. Random secrets will be sampled
   * and basic evaluation keys (multiplication / conjugation / dense-to-sparse /
   * sparse-to-dense) will be prepared automatically.
   *
   * @param context CKKS context (can be a BootContext or a Context)
   * Standard contexts use full-ring sparse secrets; MEBOOT BootContext selects
   * the Subring Prefix layout automatically.
   */
  explicit UserInterface(ContextPtr<word> context);

  // Select the Subring Prefix explicitly for isolated CtS or paired tests.
  UserInterface(ContextPtr<word> context, SparseSecretLayout sparse_layout);

  /**
   * @brief Encrypt a plaintext into a ciphertext.
   *
   * @param ctxt output ciphertext
   * @param ptxt input plaintext
   */
  void Encrypt(Ct &ctxt, const Pt &ptxt) const;

  /**
   * @brief Construct a deterministic contract ciphertext (c1 = NTT(c1_constant),
   * c0 = ptxt - c1 * s mod Q) for isolated operator testing (e.g., LCR / CtS).
   *
   * @param ctxt output ciphertext
   * @param ptxt input plaintext
   * @param c1_constant constant coefficient for c1 before NTT (default 1)
   * @param use_sparse_secret whether to encrypt under the sparse secret
   */
  void EncryptUnderContract(Ct &ctxt, const Pt &ptxt, word c1_constant = 1,
                            bool use_sparse_secret = false) const;

  /**
   * @brief Decrypt a ciphertext into a plaintext.
   *
   * @param ptxt output plaintext
   * @param ctxt input ciphertext
   */
  void Decrypt(Pt &ptxt, const Ct &ctxt) const;

  /**
   * @brief Decrypt a ciphertext encrypted under sparse secret into a plaintext.
   *
   * @param ptxt output plaintext
   * @param ctxt input ciphertext
   */
  void DecryptSparse(Pt &ptxt, const Ct &ctxt) const;

  // Get const reference to an evaluation key
  const Evk &GetRotationKey(int rot_idx) const;
  const Evk &GetMultiplicationKey() const;
  const Evk &GetConjugationKey() const;
  const Evk &GetDenseToSparseKey() const;
  const Evk &GetSparseToDenseKey() const;

  /**
   * @brief Getter for the evaluation key map.
   *
   * @return const EvkMap<word>& const reference to the evaluation key map
   */
  const EvkMap<word> &GetEvkMap() const;

  /**
   * @brief Prepare a rotation key for the given rotation distance.
   *
   * @param rot_idx rotation distance
   * @param max_level maximum level for the rotation key (default: -1 -->
   * param_->max_key_switch_level_)
   */
  void PrepareRotationKey(int rot_idx, int max_level = -1);

  /**
   * @brief Prepare rotation keys for the given EvkRequest.
   *
   * @param evk_request The request containing rotation distances and levels.
   */
  void PrepareRotationKey(const EvkRequest &evk_request);

  /**
   * @brief Getter for the AKS evaluation key map (delegates to evk_map_).
   *
   * @return const AKSKeyMap<word>& const reference to the AKS key map
   */
  const AKSKeyMap<word> &GetAKSKeyMap() const;

  /**
   * @brief Prepare AKS evaluation keys from diagonal plaintexts.
   *
   * @param rot_indices rotation distances / diagonal offsets
   * @param diagonals map of diagonal plaintexts
   * @param level level of the AKS keys (defaults to param max_level_)
   * @param from_sparse whether the input secret is sparse (default false)
   */
  void PrepareAKSKeys(const std::vector<int> &rot_indices,
                      const std::map<int, Plaintext<word>> &diagonals,
                      int level = -1, bool from_sparse = false);

#ifdef ENABLE_EXTENSION
  /**
   * @brief Prepare AKS evaluation keys from a StripedMatrix.
   *
   * @param matrix StripedMatrix containing diagonals
   * @param level level of the AKS keys
   * @param scale scaling factor for diagonal encoding
   * @param from_sparse whether the input secret is sparse (default false)
   */
  void PrepareAKSKeys(const StripedMatrix &matrix, int level, double scale,
                      bool from_sparse = false);

  /**
   * @brief Prepare Stage 2 AKS evaluation keys directly from a MEBootCtS instance.
   *
   * @param meboot_cts MEBootCtS instance
   * @param from_sparse whether the input secret is sparse (default true)
   */
  void PrepareMEBootAKSKeys(const MEBootCtS<word> &meboot_cts,
                            bool from_sparse = true);
#endif

 private:
  static inline constexpr double kErrorStandardDeviation = 3.2;
  static inline constexpr double kSubringEntryStandardDeviation = 819.2;
  static inline constexpr int kernel_block_dim_ = 256;

  ContextPtr<word> context_;
  SparseSecretLayout sparse_layout_;
  Dv main_secret_;
  Dv sparse_secret_;

  EvkMap<word> evk_map_;

  std::vector<word> all_primes_;

  DvView<word> MainSecretView(int front_ignore = 0);
  DvView<word> SparseSecretView(int front_ignore = 0);
  DvConstView<word> MainSecretConstView(int front_ignore = 0) const;
  DvConstView<word> SparseSecretConstView(int front_ignore = 0) const;

  // Initialization sequences;
  void PrepareSecrets();
  void PrepareBasicEvks();

  void PopulateEvk(Evk &evk, const NPInfo &np, const Dv &encryption_secret,
                   const Dv &target_secret,
                   double error_sigma = kErrorStandardDeviation);

  void SampleRandomPolynomial(Dv &poly, const NPInfo &np) const;
  void SampleError(Dv &poly, const NPInfo &np,
                   double error_sigma = kErrorStandardDeviation) const;

  NPInfo GetNPForEvk(int max_level) const;
};

}  // namespace cheddar
