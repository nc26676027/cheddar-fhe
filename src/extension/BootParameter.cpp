#include "extension/BootParameter.h"

#include "common/CommonUtils.h"
#include "common/Assert.h"
#include <cmath>

namespace cheddar {

BootParameter::BootParameter(int max_level, int num_cts_levels,
                             int num_stc_levels, int log_message_ratio /* = 5*/,
                             bool use_meboot_cts /* = false*/,
                             int num_double_angle /* = 3*/)
    : max_level_{max_level},
      num_cts_levels_{use_meboot_cts ? 2 : num_cts_levels},
      num_prefix_levels_{use_meboot_cts ? 1 : 0},
      num_stc_levels_{num_stc_levels},
      log_message_ratio_{log_message_ratio},
      use_meboot_cts_{use_meboot_cts},
      mod_coefficients_{[num_double_angle] {
        std::vector<double> coefficients{
          0.12517186708929745802,    0.0, 0.2894364973331168731,      0.0,
          0.36272381596524499154,    0.0, 0.3011054704600794278,      0.0,
          -0.10550875667295944105,   0.0, -0.43588877795190139706,    0.0,
          0.37482647434055190702,    0.0, -0.14821069913569220404,    0.0,
          0.03665437786710548091,    0.0, -0.0063882548960017121343,  0.0,
          0.00083684232451067872756, 0.0, -8.6443599931576702305e-05, 0.0,
          7.0966437900548814324e-06, 0.0, -5.228015817181348194e-07,  0.0,
          2.2714690137973883081e-08, 0.0, -2.3761936068138980797e-09};
        if (num_double_angle != 3) {
          // The legacy polynomial is fitted to the K=16 discrete intervals.
          // K=64 needs a uniform approximation over the full reduced interval.
          // These even Chebyshev coefficients of cos(4*pi*x)/(2*pi)^(1/32)
          // were generated with 220-bit Bessel arithmetic; degree 46 tail <1e-22.
          coefficients = {
              .1487160428841549364, 0, .3438752823000741365, 0,
              .4309420959533869500, 0, .3577356358257063329, 0,
              -.1253384879085475412, 0, -.5178437987375604790, 0,
              .4453125203501687723, 0, -.1760759042715158706, 0,
              .04354895109769222478, 0, -.007588205952703277156, 0,
              9.948346299771446653e-4, 0, -1.024181662263084418e-4, 0,
              8.535694573847822357e-6, 0, -5.892844320478625263e-7, 0,
              3.431606715447770577e-8, 0, -1.710546107089388921e-9, 0,
              7.388094028782322708e-11, 0, -2.793626800502910719e-12, 0,
              9.330094064120101088e-14, 0, -2.773478377162341650e-15, 0,
              7.387825442598294452e-17, 0, -1.774020573143411128e-18, 0,
              3.860727067617261585e-20, 0, -7.651229430959767231e-22, 0};
          return coefficients;
        }
        return coefficients;
      }()},
      num_double_angle_{num_double_angle},
      initial_K_{2} {
  AssertTrue(num_double_angle == 3 || num_double_angle == 5,
             "EvalMod supports the validated K=16 or K=64 polynomial");
}

int BootParameter::GetNumEvalModLevels() const {
  return Log2Ceil(mod_coefficients_.size()) + num_double_angle_;
}

int BootParameter::GetMaxLevel() const { return max_level_; }
int BootParameter::GetCtSStartLevel() const { return max_level_; }
int BootParameter::GetEvalModStartLevel() const {
  return max_level_ - num_prefix_levels_ - num_cts_levels_;
}
int BootParameter::GetStCStartLevel() const {
  return GetEvalModStartLevel() - GetNumEvalModLevels();
}
int BootParameter::GetStartLevel() const { return max_level_; }
int BootParameter::GetEndLevel() const {
  return GetStCStartLevel() - num_stc_levels_;
}

}  // namespace cheddar
