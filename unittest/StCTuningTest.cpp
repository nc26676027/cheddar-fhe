#include "Testbed.h"

#include "extension/EvalSpecialFFT.h"

namespace {

template <typename word>
void TuneStC(Testbed<word> &bed) {
  constexpr int slots = 1 << 15;
  // Skip preparing unrelated CtS matrices while keeping the real StC levels.
  auto standard = std::dynamic_pointer_cast<BootContext<word>>(bed.context_);
  BootParameter boot(bed.param_->max_level_, 2, 3, 5, true,
                     standard->boot_param_.num_double_angle_);
  const std::vector<std::vector<int>> bases{{8, 8, 8}, {8, 8, 8}, {16, 16, 16}};
  std::vector<std::unique_ptr<EvalSpecialFFT<word>>> transforms;
  EvkRequest req;
  for (size_t k = 0; k < bases.size(); ++k) {
    transforms.push_back(std::make_unique<EvalSpecialFFT<word>>(
        bed.context_, boot, slots, 1.0, 1.0, bases[k], k != 0));
    transforms.back()->AddRequiredRotations(req, false);
  }
  bed.interface_->PrepareRotationKey(req);
  const auto &keys = bed.interface_->GetEvkMap();
  for (size_t config = 0; config < transforms.size(); ++config) {
    for (size_t stage = 0; stage < transforms[config]->GetStCPhases().size(); ++stage) {
      const auto &phase = transforms[config]->GetStCPhases()[stage];
      auto np = bed.param_->LevelToNP(phase.GetLevel());
      auto &handler = bed.context_->mod_switch_handlers_.at(phase.GetLevel());
      int active = 0;
      for (int i = 0; i < handler.GetBeta(); ++i) active += handler.IsChunkActive(i);
      std::cout << "STC_PLAN," << sizeof(word) * 8 << ',' << config << ',' << stage
                << ',' << phase.GetLevel() << ',' << np.GetNumQ() << ',' << bed.param_->alpha_
                << ',' << active << ',' << phase.GetNumDiagonals()
                << ',' << phase.GetNumBabySteps() << ',' << phase.GetNumGiantSteps() << '\n';
      std::cout << "STC_COMPACT," << sizeof(word) * 8 << ',' << config << ',' << stage
                << ',' << phase.GetNumCompactDiagonals() << ',' << phase.GetCompactBytes() << '\n';
    }
  }
  std::vector<Complex> message(slots);
  for (int j = 0; j < slots; ++j) {
    message[j] = Complex(0.002 * std::sin(j * 0.013), 0.002 * std::cos(j * 0.017));
  }
  Ciphertext<word> input;
  bed.EncodeAndEncrypt(input, message, boot.GetStCStartLevel());
  std::vector<Ciphertext<word>> outputs(transforms.size());
  for (int warmup = 0; warmup < 5; ++warmup) {
    for (size_t k = 0; k < transforms.size(); ++k) {
      transforms[k]->EvaluateStC(bed.context_, outputs[k], input, keys, false);
    }
  }
  cudaEvent_t start, end;
  cudaEventCreate(&start);
  cudaEventCreate(&end);
  for (int repeat = 0; repeat < 30; ++repeat) {
    for (size_t offset = 0; offset < transforms.size(); ++offset) {
      size_t k = (repeat + offset) % transforms.size();
      cudaDeviceSynchronize();
      cudaEventRecord(start);
      auto begin = std::chrono::steady_clock::now();
      transforms[k]->EvaluateStC(bed.context_, outputs[k], input, keys, false);
      cudaEventRecord(end);
      cudaEventSynchronize(end);
      float gpu;
      cudaEventElapsedTime(&gpu, start, end);
      double wall = std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - begin).count();
      std::cout << "STC_SAMPLE," << sizeof(word) * 8 << ',' << k << ',' << repeat
                << ',' << wall << ',' << gpu << '\n';
    }
  }
  cudaEventDestroy(start);
  cudaEventDestroy(end);
  std::vector<Complex> reference;
  bed.DecryptAndDecode(reference, outputs[0]);
  HostVector<word> original_b, original_a, optimized_b, optimized_a;
  CopyDeviceToHost(original_b, outputs[0].bx_);
  CopyDeviceToHost(original_a, outputs[0].ax_);
  CopyDeviceToHost(optimized_b, outputs[1].bx_);
  CopyDeviceToHost(optimized_a, outputs[1].ax_);
  EXPECT_TRUE(original_b == optimized_b) << "Compact GS changed Bx residues";
  EXPECT_TRUE(original_a == optimized_a) << "Compact GS changed Ax residues";
  for (size_t k = 1; k < outputs.size(); ++k) {
    EXPECT_EQ(outputs[k].GetNP(), outputs[0].GetNP());
    EXPECT_EQ(outputs[k].GetScale(), outputs[0].GetScale());
    EXPECT_EQ(outputs[k].GetNumSlots(), outputs[0].GetNumSlots());
    std::vector<Complex> obtained;
    bed.DecryptAndDecode(obtained, outputs[k]);
    bed.CompareMessages(reference, obtained, true, 1e-4);
  }
}

}  // namespace

TEST_P(Testbed32, StCTuning) { TuneStC(*this); }
TEST_P(Testbed64, StCTuning) { TuneStC(*this); }
INSTANTIATE_TEST_SUITE_P(Cheddar, Testbed32, testing::Values("bootparam_40.json"));
INSTANTIATE_TEST_SUITE_P(Cheddar, Testbed64, testing::Values("bootparam_40_64bit.json"));
