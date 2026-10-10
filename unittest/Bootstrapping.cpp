#include "Testbed.h"

static constexpr int num_slots = 1 << 15;

static constexpr int warm_up = 5;

TEST_P(Testbed32, Bootstrap) {
  using word = uint32_t;
  std::cout << "Preparing for bootstrapping (num_slots: " << num_slots << ")"
            << std::endl;
  std::shared_ptr<BootContext<word>> boot_context =
      std::dynamic_pointer_cast<BootContext<word>>(context_);
  boot_context->PrepareEvalMod();
  boot_context->PrepareEvalSpecialFFT(num_slots);
  EvkRequest req;
  boot_context->AddRequiredRotations(req, num_slots);
  interface_->PrepareRotationKey(req);

  std::vector<Complex> msg1;
  GenerateRandomMessage(msg1, num_slots);
  Ciphertext<word> ct1;

  Ciphertext<word> ct_res;
  std::vector<Complex> res;

  __ProfileStart("Boot-Basic", warm_up, EncodeAndEncrypt(ct1, msg1, 0));
  boot_context->Boot(ct_res, ct1, interface_->GetEvkMap());
  __ProfileEnd("Boot-Basic");

  // check correctness
  DecryptAndDecode(res, ct_res);
  CompareMessages(msg1, res);

  __ProfileStart("Boot-MinKS", warm_up, EncodeAndEncrypt(ct1, msg1, 0));
  boot_context->Boot(ct_res, ct1, interface_->GetEvkMap(), true);
  __ProfileEnd("Boot-MinKS");

  // check correctness
  DecryptAndDecode(res, ct_res);
  CompareMessages(msg1, res);
}

TEST_P(Testbed32, MEBootBootstrap) {
  using word = uint32_t;
  if (std::string(GetParam()) != "bootparam_40.json") {
    GTEST_SKIP() << "MEBootCtS test is only configured for bootparam_40.json";
  }
  std::cout << "Preparing for MEBoot bootstrapping (num_slots: " << num_slots
            << ")" << std::endl;

  // Re-create BootContext and UserInterface with use_meboot_cts = true
  interface_.reset();
  context_.reset();

  BootParameter meboot_param(param_->max_level_ - 1, 3, 3, 5,
                             /*use_meboot_cts=*/true);
  context_ = BootContext<word>::Create(*param_, meboot_param);
  interface_ = std::make_unique<UserInterface<word>>(context_);

  std::shared_ptr<BootContext<word>> boot_context =
      std::dynamic_pointer_cast<BootContext<word>>(context_);
  boot_context->PrepareEvalMod();
  boot_context->PrepareEvalSpecialFFT(num_slots);

  // Prepare AKS keys for Stage 2 (from sparse to main secret)
  const auto &meboot_cts = boot_context->GetMEBootCtS(num_slots);
  EXPECT_EQ(meboot_cts.GetStartLevel(), param_->max_level_ - 1);
  EXPECT_EQ(meboot_cts.GetEndLevel(), param_->max_level_ - 4);
  interface_->PrepareAKSKeys(meboot_cts.GetStage2Matrix(),
                             meboot_cts.GetStage2Level(),
                             meboot_cts.GetStage2LCRScale(),
                             /*from_sparse=*/true);

  // Prepare rotation keys (for both min_ks = false and min_ks = true)
  EvkRequest req;
  boot_context->AddRequiredRotations(req, num_slots, /*min_ks=*/false);
  boot_context->AddRequiredRotations(req, num_slots, /*min_ks=*/true);
  interface_->PrepareRotationKey(req);

  std::vector<Complex> msg1;
  GenerateRandomMessage(msg1, num_slots);
  Ciphertext<word> ct1;

  Ciphertext<word> ct_res;
  std::vector<Complex> res;

  __ProfileStart("MEBoot-Basic", warm_up, EncodeAndEncrypt(ct1, msg1, 0));
  boot_context->Boot(ct_res, ct1, interface_->GetEvkMap(),
                     interface_->GetAKSKeyMap(), /*min_ks=*/false);
  __ProfileEnd("MEBoot-Basic");

  // Verify level conservation: 27 -> 13 consumes 14 levels (saving 1 modulus level vs 28 -> 13 = 15 levels)
  int out_level = context_->param_.NPToLevel(ct_res.GetNP());
  EXPECT_EQ(out_level, 13);

  // Check correctness & SNR
  DecryptAndDecode(res, ct_res);
  CompareMessages(msg1, res);

  __ProfileStart("MEBoot-MinKS", warm_up, EncodeAndEncrypt(ct1, msg1, 0));
  boot_context->Boot(ct_res, ct1, interface_->GetEvkMap(),
                     interface_->GetAKSKeyMap(), /*min_ks=*/true);
  __ProfileEnd("MEBoot-MinKS");
  EXPECT_EQ(context_->param_.NPToLevel(ct_res.GetNP()), 13);
  DecryptAndDecode(res, ct_res);
  CompareMessages(msg1, res);

  // Reset local boot_context so context_.use_count() == 1 in TearDown
  boot_context.reset();
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

TEST_P(Testbed64, Bootstrap) {
  using word = uint64_t;
  std::cout << "Preparing for bootstrapping (num_slots: " << num_slots << ")"
            << std::endl;
  std::shared_ptr<BootContext<word>> boot_context =
      std::dynamic_pointer_cast<BootContext<word>>(context_);
  boot_context->PrepareEvalMod();
  boot_context->PrepareEvalSpecialFFT(num_slots);
  EvkRequest req;
  boot_context->AddRequiredRotations(req, num_slots);
  interface_->PrepareRotationKey(req);

  std::vector<Complex> msg1;
  GenerateRandomMessage(msg1, num_slots);
  Ciphertext<word> ct1;

  Ciphertext<word> ct_res;
  std::vector<Complex> res;

  __ProfileStart("Boot-Basic", warm_up, EncodeAndEncrypt(ct1, msg1, 0));
  boot_context->Boot(ct_res, ct1, interface_->GetEvkMap());
  __ProfileEnd("Boot-Basic");

  // check correctness
  DecryptAndDecode(res, ct_res);
  CompareMessages(msg1, res);

  __ProfileStart("Boot-MinKS", warm_up, EncodeAndEncrypt(ct1, msg1, 0));
  boot_context->Boot(ct_res, ct1, interface_->GetEvkMap(), true);
  __ProfileEnd("Boot-MinKS");

  // check correctness
  DecryptAndDecode(res, ct_res);
  CompareMessages(msg1, res);
}

TEST_P(Testbed64, MEBootBootstrap) {
  using word = uint64_t;
  if (std::string(GetParam()) != "bootparam_40_64bit.json") {
    GTEST_SKIP() << "MEBootCtS test is only configured for bootparam_40_64bit.json";
  }
  std::cout << "Preparing for MEBoot bootstrapping (num_slots: " << num_slots
            << ")" << std::endl;

  // Re-create BootContext and UserInterface with use_meboot_cts = true
  interface_.reset();
  context_.reset();

  BootParameter meboot_param(param_->max_level_, 3, 3, 5,
                             /*use_meboot_cts=*/true);
  context_ = BootContext<word>::Create(*param_, meboot_param);
  interface_ = std::make_unique<UserInterface<word>>(context_);

  std::shared_ptr<BootContext<word>> boot_context =
      std::dynamic_pointer_cast<BootContext<word>>(context_);
  boot_context->PrepareEvalMod();
  boot_context->PrepareEvalSpecialFFT(num_slots);

  // Prepare AKS keys for Stage 2 (from sparse to main secret)
  const auto &meboot_cts = boot_context->GetMEBootCtS(num_slots);
  EXPECT_EQ(meboot_cts.GetStartLevel(), param_->max_level_);
  EXPECT_EQ(meboot_cts.GetEndLevel(), param_->max_level_ - 3);
  interface_->PrepareAKSKeys(meboot_cts.GetStage2Matrix(),
                             meboot_cts.GetStage2Level(),
                             meboot_cts.GetStage2LCRScale(),
                             /*from_sparse=*/true);

  // Prepare rotation keys (for both min_ks = false and min_ks = true)
  EvkRequest req;
  boot_context->AddRequiredRotations(req, num_slots, /*min_ks=*/false);
  boot_context->AddRequiredRotations(req, num_slots, /*min_ks=*/true);
  interface_->PrepareRotationKey(req);

  std::vector<Complex> msg1;
  GenerateRandomMessage(msg1, num_slots);
  Ciphertext<word> ct1;

  Ciphertext<word> ct_res;
  std::vector<Complex> res;

  __ProfileStart("MEBoot-Basic", warm_up, EncodeAndEncrypt(ct1, msg1, 0));
  boot_context->Boot(ct_res, ct1, interface_->GetEvkMap(),
                     interface_->GetAKSKeyMap(), /*min_ks=*/false);
  __ProfileEnd("MEBoot-Basic");

  // Verify level conservation: output level should be 11 (saving 1 modulus level, 11 vs 10)
  int out_level = context_->param_.NPToLevel(ct_res.GetNP());
  EXPECT_EQ(out_level, 11);

  // Check correctness & SNR
  DecryptAndDecode(res, ct_res);
  CompareMessages(msg1, res);

  __ProfileStart("MEBoot-MinKS", warm_up, EncodeAndEncrypt(ct1, msg1, 0));
  boot_context->Boot(ct_res, ct1, interface_->GetEvkMap(),
                     interface_->GetAKSKeyMap(), /*min_ks=*/true);
  __ProfileEnd("MEBoot-MinKS");

  EXPECT_EQ(context_->param_.NPToLevel(ct_res.GetNP()), 11);
  DecryptAndDecode(res, ct_res);
  CompareMessages(msg1, res);

  // Reset local boot_context so context_.use_count() == 1 in TearDown
  boot_context.reset();
}

INSTANTIATE_TEST_SUITE_P(
    Cheddar, Testbed64, testing::Values("bootparam_40_64bit.json"),
    [](const testing::TestParamInfo<Testbed64::ParamType> &info) {
      std::string param_name = info.param;
      std::replace(param_name.begin(), param_name.end(), '.', '_');
      return param_name;
    });