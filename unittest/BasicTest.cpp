#undef ENABLE_EXTENSION

#include <chrono>
#include <cstdlib>

#include "Testbed.h"

// Correctness-only regressions still check every level without timed warmups.
static const int warm_up = [] {
  const char *fast = std::getenv("CHEDDAR_REGRESSION_FAST");
  return fast && std::string(fast) == "1" ? 0 : 5;
}();
using word = uint32_t;

TEST_P(Testbed32, EncodeDecode) {
  std::cout << "Encode and Decode functions exist for test purposes and their "
               "performance is not a priority."
            << std::endl;
  for (int level = 0; level <= param_->max_key_switch_level_; level++) {
    std::vector<Complex> msg1;
    GenerateRandomMessage(msg1);

    Plaintext<word> pt1;
    Encode(pt1, msg1, level);

    std::vector<Complex> res;
    Decode(res, pt1);
    CompareMessages(msg1, res, level == param_->max_key_switch_level_);
  }
}

TEST_P(Testbed32, EncodeEncryptDecryptDecode) {
  std::cout << "Encode, Encrypt, Decrypt and Decode functions exist for test "
               "purposes and their performance is not a priority."
            << std::endl;
  for (int level = 0; level <= param_->max_key_switch_level_; level++) {
    std::vector<Complex> msg1;
    GenerateRandomMessage(msg1);

    Ciphertext<word> ct1;
    EncodeAndEncrypt(ct1, msg1, level);

    std::vector<Complex> res;
    DecryptAndDecode(res, ct1);
    CompareMessages(msg1, res, level == param_->max_key_switch_level_);
  }
}

TEST_P(Testbed32, CtAddCt) {
  for (int level = 0; level <= param_->max_key_switch_level_; level++) {
    std::vector<Complex> msg1, msg2;
    std::vector<Complex> true_res;
    GenerateRandomMessage(msg1);
    GenerateRandomMessage(msg2);
    for (int i = 0; i < static_cast<int>(msg1.size()); i++) {
      true_res.push_back(msg1[i] + msg2[i]);
    }
    Ciphertext<word> ct1, ct2;
    Ciphertext<word> ct_res;

    std::string name = "CtAddCt at level" + std::to_string(level);
    auto prepare_cts = [&]() {
      EncodeAndEncrypt(ct1, msg1, level);
      EncodeAndEncrypt(ct2, msg2, level);
    };

    __ProfileStart(name, warm_up, prepare_cts());
    context_->Add(ct_res, ct1, ct2);
    __ProfileEnd(name);

    std::vector<Complex> res;
    DecryptAndDecode(res, ct_res);
    CompareMessages(true_res, res, level == param_->max_key_switch_level_);
  }
}

TEST_P(Testbed32, CtAddPt) {
  for (int level = 0; level <= param_->max_key_switch_level_; level++) {
    std::vector<Complex> msg1, msg2;
    std::vector<Complex> true_res;
    GenerateRandomMessage(msg1);
    GenerateRandomMessage(msg2);
    for (int i = 0; i < static_cast<int>(msg1.size()); i++) {
      true_res.push_back(msg1[i] + msg2[i]);
    }
    Ciphertext<word> ct1;
    Plaintext<word> pt2;
    Ciphertext<word> ct_res;

    std::string name = "CtAddPt at level" + std::to_string(level);
    auto prepare_cts = [&]() {
      EncodeAndEncrypt(ct1, msg1, level);
      Encode(pt2, msg2, level);
    };

    __ProfileStart(name, warm_up, prepare_cts());
    context_->Add(ct_res, ct1, pt2);
    __ProfileEnd(name);

    std::vector<Complex> res;
    DecryptAndDecode(res, ct_res);
    CompareMessages(true_res, res, level == param_->max_key_switch_level_);
  }
}

TEST_P(Testbed32, CtAddConst) {
  for (int level = 0; level <= param_->max_key_switch_level_; level++) {
    std::vector<Complex> msg1, msg2;
    std::vector<Complex> true_res;
    GenerateRandomMessage(msg1);
    GenerateRandomMessage(msg2);
    double const_value = msg2[0].real();
    for (int i = 0; i < static_cast<int>(msg1.size()); i++) {
      true_res.push_back(msg1[i] + const_value);
    }
    Ciphertext<word> ct1;
    Constant<word> const2;
    Ciphertext<word> ct_res;

    std::string name = "CtAddConst at level" + std::to_string(level);
    auto prepare_cts = [&]() {
      EncodeAndEncrypt(ct1, msg1, level);
      EncodeConstant(const2, const_value, level);
    };

    __ProfileStart(name, warm_up, prepare_cts());
    context_->Add(ct_res, ct1, const2);
    __ProfileEnd(name);

    std::vector<Complex> res;
    DecryptAndDecode(res, ct_res);
    CompareMessages(true_res, res, level == param_->max_key_switch_level_);
  }
}

TEST_P(Testbed32, CtSubCt) {
  for (int level = 0; level <= param_->max_key_switch_level_; level++) {
    std::vector<Complex> msg1, msg2;
    std::vector<Complex> true_res;
    GenerateRandomMessage(msg1);
    GenerateRandomMessage(msg2);
    for (int i = 0; i < static_cast<int>(msg1.size()); i++) {
      true_res.push_back(msg1[i] - msg2[i]);
    }
    Ciphertext<word> ct1, ct2;
    Ciphertext<word> ct_res;

    std::string name = "CtSubCt at level" + std::to_string(level);
    auto prepare_cts = [&]() {
      EncodeAndEncrypt(ct1, msg1, level);
      EncodeAndEncrypt(ct2, msg2, level);
    };

    __ProfileStart(name, warm_up, prepare_cts());
    context_->Sub(ct_res, ct1, ct2);
    __ProfileEnd(name);

    std::vector<Complex> res;
    DecryptAndDecode(res, ct_res);
    CompareMessages(true_res, res, level == param_->max_key_switch_level_);
  }
}

TEST_P(Testbed32, CtSubPt) {
  for (int level = 0; level <= param_->max_key_switch_level_; level++) {
    std::vector<Complex> msg1, msg2;
    std::vector<Complex> true_res;
    GenerateRandomMessage(msg1);
    GenerateRandomMessage(msg2);
    for (int i = 0; i < static_cast<int>(msg1.size()); i++) {
      true_res.push_back(msg1[i] - msg2[i]);
    }
    Ciphertext<word> ct1;
    Plaintext<word> pt2;
    Ciphertext<word> ct_res;

    std::string name = "CtSubPt at level" + std::to_string(level);
    auto prepare_cts = [&]() {
      EncodeAndEncrypt(ct1, msg1, level);
      Encode(pt2, msg2, level);
    };

    __ProfileStart(name, warm_up, prepare_cts());
    context_->Sub(ct_res, ct1, pt2);
    __ProfileEnd(name);

    std::vector<Complex> res;
    DecryptAndDecode(res, ct_res);
    CompareMessages(true_res, res, level == param_->max_key_switch_level_);
  }
}

TEST_P(Testbed32, CtSubConst) {
  for (int level = 0; level <= param_->max_key_switch_level_; level++) {
    std::vector<Complex> msg1, msg2;
    std::vector<Complex> true_res;
    GenerateRandomMessage(msg1);
    GenerateRandomMessage(msg2);
    double const_value = msg2[0].real();
    for (int i = 0; i < static_cast<int>(msg1.size()); i++) {
      true_res.push_back(msg1[i] - const_value);
    }
    Ciphertext<word> ct1;
    Constant<word> const2;
    Ciphertext<word> ct_res;

    std::string name = "CtSubConst at level" + std::to_string(level);
    auto prepare_cts = [&]() {
      EncodeAndEncrypt(ct1, msg1, level);
      EncodeConstant(const2, const_value, level);
    };

    __ProfileStart(name, warm_up, prepare_cts());
    context_->Sub(ct_res, ct1, const2);
    __ProfileEnd(name);

    std::vector<Complex> res;
    DecryptAndDecode(res, ct_res);
    CompareMessages(true_res, res, level == param_->max_key_switch_level_);
  }
}

TEST_P(Testbed32, NegCt) {
  for (int level = 0; level <= param_->max_key_switch_level_; level++) {
    std::vector<Complex> msg1;
    std::vector<Complex> true_res;
    GenerateRandomMessage(msg1);
    for (int i = 0; i < static_cast<int>(msg1.size()); i++) {
      true_res.push_back(-msg1[i]);
    }
    Ciphertext<word> ct1;
    Ciphertext<word> ct_res;

    std::string name = "NegCt at level" + std::to_string(level);
    auto prepare_cts = [&]() { EncodeAndEncrypt(ct1, msg1, level); };

    __ProfileStart(name, warm_up, prepare_cts());
    context_->Neg(ct_res, ct1);
    __ProfileEnd(name);

    std::vector<Complex> res;
    DecryptAndDecode(res, ct_res);
    CompareMessages(true_res, res, level == param_->max_key_switch_level_);
  }
}

TEST_P(Testbed32, CtMultPt) {
  for (int level = 1; level <= param_->max_key_switch_level_; level++) {
    std::vector<Complex> msg1, msg2;
    std::vector<Complex> true_res;
    GenerateRandomMessage(msg1);
    GenerateRandomMessage(msg2);
    for (int i = 0; i < static_cast<int>(msg1.size()); i++) {
      true_res.push_back(msg1[i] * msg2[i]);
    }
    Ciphertext<word> ct1;
    Plaintext<word> pt2;
    Ciphertext<word> ct_res;

    std::string name =
        "CtMultPt (w/o rescaling) at level" + std::to_string(level);
    auto prepare_cts = [&]() {
      EncodeAndEncrypt(ct1, msg1, level);
      Encode(pt2, msg2, level);
    };

    __ProfileStart(name, warm_up, prepare_cts());
    context_->Mult(ct_res, ct1, pt2);
    __ProfileEnd(name);

    std::vector<Complex> res;
    DecryptAndDecode(res, ct_res);
    CompareMessages(true_res, res, level == param_->max_key_switch_level_);
  }
}

TEST_P(Testbed32, CtMultConst) {
  for (int level = 1; level <= param_->max_key_switch_level_; level++) {
    std::vector<Complex> msg1, msg2;
    std::vector<Complex> true_res;
    GenerateRandomMessage(msg1);
    GenerateRandomMessage(msg2);
    double const_value = msg2[0].real();
    for (int i = 0; i < static_cast<int>(msg1.size()); i++) {
      true_res.push_back(msg1[i] * const_value);
    }
    Ciphertext<word> ct1;
    Constant<word> const2;
    Ciphertext<word> ct_res;

    std::string name =
        "CtMultConst (w/o rescaling) at level" + std::to_string(level);
    auto prepare_cts = [&]() {
      EncodeAndEncrypt(ct1, msg1, level);
      EncodeConstant(const2, const_value, level);
    };

    __ProfileStart(name, warm_up, prepare_cts());
    context_->Mult(ct_res, ct1, const2);
    __ProfileEnd(name);

    std::vector<Complex> res;
    DecryptAndDecode(res, ct_res);
    CompareMessages(true_res, res, level == param_->max_key_switch_level_);
  }
}

TEST_P(Testbed32, CtMultImaginaryUnit) {
  for (int level = 0; level <= param_->max_key_switch_level_; level++) {
    std::vector<Complex> msg1;
    std::vector<Complex> true_res;
    GenerateRandomMessage(msg1);
    for (int i = 0; i < static_cast<int>(msg1.size()); i++) {
      true_res.push_back(Complex(-msg1[i].imag(), msg1[i].real()));
    }
    Ciphertext<word> ct1;
    Ciphertext<word> ct_res;

    std::string name = "CtMultImaginaryUnit at level" + std::to_string(level);
    auto prepare_cts = [&]() { EncodeAndEncrypt(ct1, msg1, level); };

    __ProfileStart(name, warm_up, prepare_cts());
    context_->MultImaginaryUnit(ct_res, ct1);
    __ProfileEnd(name);

    std::vector<Complex> res;
    DecryptAndDecode(res, ct_res);
    CompareMessages(true_res, res, level == param_->max_key_switch_level_);
  }
}

TEST_P(Testbed32, HMult) {
  for (int level = 1; level <= param_->max_key_switch_level_; level++) {
    std::vector<Complex> msg1, msg2;
    std::vector<Complex> true_res;
    GenerateRandomMessage(msg1);
    GenerateRandomMessage(msg2);
    for (int i = 0; i < static_cast<int>(msg1.size()); i++) {
      true_res.push_back(msg1[i] * msg2[i]);
    }
    Ciphertext<word> ct1, ct2;
    Ciphertext<word> ct_res, ct_tmp;

    // Merged case;
    std::string name =
        "HMult(tensor + merged relin-rescale) at level" + std::to_string(level);
    auto prepare_cts = [&]() {
      EncodeAndEncrypt(ct1, msg1, level);
      EncodeAndEncrypt(ct2, msg2, level);
    };
    __ProfileStart(name, warm_up, prepare_cts(););
    context_->HMult(ct_res, ct1, ct2, interface_->GetMultiplicationKey(), true);
    __ProfileEnd(name);

    std::vector<Complex> res;
    DecryptAndDecode(res, ct_res);
    CompareMessages(true_res, res, level == param_->max_key_switch_level_);

    // Non-merged case;
    name = "HMult(tensor + relinearize) at level" + std::to_string(level);
    __ProfileStart(name, warm_up, prepare_cts(););
    context_->HMult(ct_tmp, ct1, ct2, interface_->GetMultiplicationKey(),
                    false);
    __ProfileEnd(name);

    DecryptAndDecode(res, ct_tmp);
    CompareMessages(true_res, res, level == param_->max_key_switch_level_);

    name = "Rescale at level" + std::to_string(level);
    __ProfileStart(name, warm_up,
                   context_->HMult(ct_tmp, ct1, ct2,
                                   interface_->GetMultiplicationKey(), false););
    context_->Rescale(ct_res, ct_tmp);
    __ProfileEnd(name);

    DecryptAndDecode(res, ct_res);
    CompareMessages(true_res, res, level == param_->max_key_switch_level_);
  }
}

TEST_P(Testbed32, HRot) {
  int num_slots = (1 << log_degree_) / 2;
  word test_rot_dist = 1234;
  interface_->PrepareRotationKey(test_rot_dist, param_->max_key_switch_level_);

  for (int level = 0; level <= param_->max_key_switch_level_; level++) {
    std::vector<Complex> msg1;
    std::vector<Complex> true_res;
    GenerateRandomMessage(msg1);
    for (int i = 0; i < static_cast<int>(msg1.size()); i++) {
      true_res.push_back(msg1[(i + test_rot_dist) % num_slots]);
    }
    Ciphertext<word> ct1, ct_res;
    std::string name = "HRot at level" + std::to_string(level);
    __ProfileStart(name, warm_up, EncodeAndEncrypt(ct1, msg1, level););
    context_->HRot(ct_res, ct1, interface_->GetRotationKey(test_rot_dist),
                   test_rot_dist);
    __ProfileEnd(name);

    std::vector<Complex> res;
    DecryptAndDecode(res, ct_res);
    CompareMessages(true_res, res, level == param_->max_key_switch_level_);
  }
}

TEST_P(Testbed32, HConj) {
  for (int level = 0; level <= param_->max_key_switch_level_; level++) {
    std::vector<Complex> msg1;
    std::vector<Complex> true_res;
    GenerateRandomMessage(msg1);
    for (int i = 0; i < static_cast<int>(msg1.size()); i++) {
      true_res.push_back(std::conj(msg1[i]));
    }
    Ciphertext<word> ct1, ct_res;
    std::string name = "HConj at level" + std::to_string(level);
    __ProfileStart(name, warm_up, EncodeAndEncrypt(ct1, msg1, level););
    context_->HConj(ct_res, ct1, interface_->GetConjugationKey());
    __ProfileEnd(name);

    std::vector<Complex> res;
    DecryptAndDecode(res, ct_res);
    CompareMessages(true_res, res, level == param_->max_key_switch_level_);
  }
}

TEST_P(Testbed32, AutonomousModSwitchOperations) {
  int num_slots = (1 << log_degree_) / 2;
  int rot_dist = 7;
  interface_->PrepareRotationKey(rot_dist, param_->max_key_switch_level_);
  const auto &rot_key = interface_->GetRotationKey(rot_dist);

  for (int level = 1; level <= param_->max_key_switch_level_; level++) {
    const auto &mod_switcher = context_->mod_switch_handlers_.at(level);
    NPInfo np = param_->LevelToNP(level);
    int expected_offset = param_->GetKeySwitchNP(
                              param_->max_key_switch_level_).num_ter_ - np.num_ter_;
    EXPECT_EQ(mod_switcher.GetPrimeOffset(), expected_offset);
    EXPECT_GT(mod_switcher.GetBeta(), 0);

    std::vector<Complex> msg1;
    GenerateRandomMessage(msg1);
    Ciphertext<word> ct1;
    EncodeAndEncrypt(ct1, msg1, level);

    // 1. Autonomous ModUp buffer allocation + execution
    std::vector<DeviceVector<word>> modup_buffers;
    mod_switcher.ModUp(modup_buffers, ct1.AxConstView());
    EXPECT_EQ(static_cast<int>(modup_buffers.size()), mod_switcher.GetBeta());

    // 2. MultKeyNoModDown followed by autonomous ciphertext ModDown
    Ciphertext<word> ct_qp, ct_moddown, ct_rot;
    context_->MultKeyNoModDown(ct_qp, ct1, rot_key);
    mod_switcher.ModDown(ct_moddown, ct_qp);
    context_->Permute(ct_rot, ct_moddown, rot_dist);

    std::vector<Complex> expected_rot(num_slots);
    for (int i = 0; i < num_slots; i++) {
      expected_rot[i] = msg1[(i + rot_dist) % num_slots];
    }
    std::vector<Complex> obtained_rot;
    DecryptAndDecode(obtained_rot, ct_rot);
    CompareMessages(expected_rot, obtained_rot, false);

    // 3. Autonomous ciphertext ModDownAndRescale & Rescale
    std::vector<Complex> msg2;
    GenerateRandomMessage(msg2);
    Ciphertext<word> ct2, ct_tensor, ct_relin_qp, ct_md_rescale, ct_relin, ct_rescale;
    EncodeAndEncrypt(ct2, msg2, level);
    context_->Mult(ct_tensor, ct1, ct2);
    context_->MultKeyNoModDown(ct_relin_qp, ct_tensor,
                               interface_->GetMultiplicationKey());
    mod_switcher.ModDownAndRescale(ct_md_rescale, ct_relin_qp);
    mod_switcher.ModDown(ct_relin, ct_relin_qp);
    mod_switcher.Rescale(ct_rescale, ct_relin);

    std::vector<Complex> expected_mult(num_slots);
    for (int i = 0; i < num_slots; i++) {
      expected_mult[i] = msg1[i] * msg2[i];
    }
    std::vector<Complex> dec_md_rescale, dec_rescale;
    DecryptAndDecode(dec_md_rescale, ct_md_rescale);
    DecryptAndDecode(dec_rescale, ct_rescale);
    CompareMessages(expected_mult, dec_md_rescale, false);
    CompareMessages(expected_mult, dec_rescale, false);
  }
}

INSTANTIATE_TEST_SUITE_P(
    Cheddar, Testbed32,
    testing::Values("bootparam_30.json", "bootparam_35.json",
                    "bootparam_40.json"),
    [](const testing::TestParamInfo<Testbed32::ParamType> &info) {
      std::string param_name = info.param;
      std::replace(param_name.begin(), param_name.end(), '.', '_');
      return param_name;
    });
