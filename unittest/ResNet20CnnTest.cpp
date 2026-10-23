#include <cuda_runtime.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>

#include "UserInterface.h"
#include "core/Context.h"
#include "extension/BootContext.h"
#include "extension/BootParameter.h"
#include "extension/Func15RBootExecution.h"
#include "extension/FunctionalEvalMod.h"
#include "extension/cnn/ResNet20.h"
#include "extension/cnn/SplitAndQuadruple.h"
#include "extension/cnn/TensorShape.h"
#include "nlohmann/json.hpp"

using namespace cheddar;
using namespace cheddar::cnn;

namespace {

using Tensor = std::vector<std::vector<std::vector<double>>>;  // [c][h][w]

std::string FindFixturePath(const std::string &filename) {
  const std::vector<std::string> candidates = {
      filename, "unittest/fixtures/" + filename,
      "../unittest/fixtures/" + filename, "../../unittest/fixtures/" + filename};
  for (const auto &p : candidates) {
    std::ifstream f(p);
    if (f.good()) return p;
  }
  return filename;
}

nlohmann::json LoadJson(const std::string &name) {
  std::ifstream f(FindFixturePath(name));
  EXPECT_TRUE(f.is_open()) << "Cannot open " << name;
  nlohmann::json j;
  f >> j;
  return j;
}

ConvLayer ParseConv(const nlohmann::json &j) {
  ConvLayer layer;
  layer.in_ch = j["in_ch"].get<int>();
  layer.out_ch = j["out_ch"].get<int>();
  layer.stride = j["stride"].get<int>();
  layer.bias = j["bias"].get<std::vector<double>>();
  auto raw = j["weight"].get<std::vector<double>>();
  layer.weight.assign(layer.out_ch,
                      Tensor(layer.in_ch, std::vector<std::vector<double>>(
                                              3, std::vector<double>(3))));
  size_t idx = 0;
  for (auto &o : layer.weight)
    for (auto &i : o)
      for (auto &r : i)
        for (auto &v : r) v = raw.at(idx++);
  return layer;
}

ResNet20Weights LoadWeights() {
  auto j = LoadJson("resnet20_folded.json");
  ResNet20Weights w;
  w.stem = ParseConv(j["stem"]);
  for (int i = 0; i < ResNet20Weights::kBlocks; ++i) {
    const std::string prefix = "layer" + std::to_string(i / 3 + 1) + "_" +
                               std::to_string(i % 3) + "_";
    w.conv1[i] = ParseConv(j[prefix + "conv1"]);
    w.conv2[i] = ParseConv(j[prefix + "conv2"]);
  }
  w.fc_weight = j["fc"]["weight"].get<std::vector<double>>();
  w.fc_bias = j["fc"]["bias"].get<std::vector<double>>();
  return w;
}

Tensor LoadImage(int &label) {
  auto j = LoadJson("cifar10_index0.json");
  label = j["label"].get<int>();
  auto raw = j["values"].get<std::vector<double>>();
  Tensor img(3, std::vector<std::vector<double>>(32, std::vector<double>(32)));
  size_t idx = 0;
  for (auto &c : img)
    for (auto &r : c)
      for (auto &v : r) v = raw.at(idx++);
  return img;
}

// ---------------- float64 reference forward ----------------

Tensor Conv2D(const Tensor &x, const ConvLayer &layer, int stride) {
  const int h = x[0].size(), w = x[0][0].size();
  const int oh = (h + 2 - 3) / stride + 1, ow = (w + 2 - 3) / stride + 1;
  Tensor out(layer.out_ch,
             std::vector<std::vector<double>>(oh, std::vector<double>(ow)));
  for (int o = 0; o < layer.out_ch; ++o)
    for (int i = 0; i < oh; ++i)
      for (int j = 0; j < ow; ++j) {
        double sum = layer.bias[o];
        for (int c = 0; c < layer.in_ch && c < (int)x.size(); ++c)
          for (int kh = 0; kh < 3; ++kh) {
            const int r = i * stride + kh - 1;
            if (r < 0 || r >= h) continue;
            for (int kw = 0; kw < 3; ++kw) {
              const int s = j * stride + kw - 1;
              if (s < 0 || s >= w) continue;
              sum += x[c][r][s] * layer.weight[o][c][kh][kw];
            }
          }
        out[o][i][j] = sum;
      }
  return out;
}

template <typename F>
Tensor Map(const Tensor &x, F f) {
  Tensor out = x;
  for (auto &c : out)
    for (auto &r : c)
      for (auto &v : r) v = f(v);
  return out;
}

Tensor AddT(const Tensor &a, const Tensor &b) {
  Tensor out = a;
  for (size_t c = 0; c < a.size(); ++c)
    for (size_t i = 0; i < a[c].size(); ++i)
      for (size_t j = 0; j < a[c][i].size(); ++j) out[c][i][j] += b[c][i][j];
  return out;
}

Tensor OptionAShortcut(const Tensor &h, int out_ch) {
  const int in_ch = h.size(), oh = h[0].size() / 2, ow = h[0][0].size() / 2;
  const int offset = (out_ch - in_ch) / 2;
  Tensor out(out_ch,
             std::vector<std::vector<double>>(oh, std::vector<double>(ow, 0.0)));
  for (int c = 0; c < in_ch; ++c)
    for (int i = 0; i < oh; ++i)
      for (int j = 0; j < ow; ++j) out[c + offset][i][j] = h[c][2 * i][2 * j];
  return out;
}

struct PlainTrace {
  std::vector<Tensor> sites;  // in ResNet20Classifier::RefreshSites() order
  std::vector<double> logits;
};

template <typename Act>
PlainTrace PlainForward(const ResNet20Weights &w, const Tensor &img, Act act) {
  PlainTrace trace;
  Tensor h = Map(Conv2D(img, w.stem, 1), act);
  trace.sites.push_back(h);
  for (int i = 0; i < ResNet20Weights::kBlocks; ++i) {
    const bool transition = (i % 3 == 0) && i > 0;
    Tensor shortcut = h;
    if (transition) {
      shortcut = OptionAShortcut(h, w.conv2[i].out_ch);
      trace.sites.push_back(shortcut);
    }
    Tensor a = Map(Conv2D(h, w.conv1[i], transition ? 2 : 1), act);
    trace.sites.push_back(a);
    h = Map(AddT(Conv2D(a, w.conv2[i], 1), shortcut), act);
    trace.sites.push_back(h);
  }
  std::vector<double> feat(h.size());
  for (size_t c = 0; c < h.size(); ++c) {
    double s = 0;
    for (auto &r : h[c])
      for (double v : r) s += v;
    feat[c] = s / 64.0;
  }
  trace.logits.assign(10, 0.0);
  for (int o = 0; o < 10; ++o) {
    double s = w.fc_bias[o];
    for (int c = 0; c < 64; ++c) s += w.fc_weight[o * 64 + c] * feat[c];
    trace.logits[o] = s;
  }
  return trace;
}

int Argmax(const std::vector<double> &v) {
  return std::max_element(v.begin(), v.end()) - v.begin();
}

std::vector<double> GoldenLogits() {
  return LoadJson("resnet20_golden_logits_index0.json")["logits"]
      .get<std::vector<double>>();
}

// ---------------- tests ----------------

TEST(ResNet20CnnTest, PlaintextPolynomialModelMatchesGoldenLogits) {
  auto weights = LoadWeights();
  int label = -1;
  auto img = LoadImage(label);
  ASSERT_EQ(label, 3);

  auto profile = BootParameter::Func15RBoot();
  auto param = profile.MakeParameter();
  auto context = BootContext<uint64_t>::Create(*param, profile);
  context->Prepare();
  const auto &evalmod = dynamic_cast<const Func15RBootExecution &>(
                            context->GetRouteExecution())
                            .GetEvalMod();
  const double g = ResNet20Classifier::kActivationGain;
  auto poly = [&](double x) {
    return g * evalmod.PlainEvaluate(x / g, FunctionalMode::kActivation);
  };
  auto trace = PlainForward(weights, img, poly);
  auto golden = GoldenLogits();
  double max_err = 0.0;
  for (int i = 0; i < 10; ++i)
    max_err = std::max(max_err, std::abs(trace.logits[i] - golden[i]));
  std::cout << "[REFERENCE] C++ polynomial model vs golden logits max err: "
            << max_err << std::endl;
  EXPECT_LT(max_err, 0.001);
  EXPECT_EQ(Argmax(trace.logits), 3);
}

TEST(ResNet20CnnTest, SplitAndQuadrupleLayoutPlaintext) {
  const int slots = 16384;
  auto pre = TensorShape::Create(32, 16, slots);
  auto post = TensorShape::Create(16, 64, slots);
  Tensor x(16, std::vector<std::vector<double>>(32, std::vector<double>(32)));
  for (int c = 0; c < 16; ++c)
    for (int i = 0; i < 32; ++i)
      for (int j = 0; j < 32; ++j) x[c][i][j] = std::sin(0.1 * c + 0.07 * i + 0.013 * j * j);
  auto in = PackChannels(x, pre, slots);
  // Simulate the masked-free layout: garbage outside the valid grid.
  std::vector<Complex> conj(slots);
  for (int k = 0; k < slots; ++k) conj[k] = std::conj(in[k]);
  std::vector<Complex> out(slots, Complex(0, 0));
  for (int part = 0; part < 2; ++part) {
    auto m = SplitAndQuadruple::BuildMatrix(pre, post, slots, part == 1);
    const auto &v = part ? conj : in;
    for (const auto &[d, diag] : m)
      for (int k = 0; k < slots; ++k) out[k] += diag[k] * v[(k + d) % slots];
  }
  auto got = UnpackChannels(out, post);
  double max_err = 0.0;
  for (int c = 0; c < 16; ++c)
    for (int q = 0; q < 4; ++q)
      for (int i = 0; i < 16; ++i)
        for (int j = 0; j < 16; ++j) {
          const double want = x[c][2 * i + q / 2][2 * j + q % 2];
          max_err = std::max(max_err, std::abs(got[4 * c + q][i][j] - want));
        }
  EXPECT_LT(max_err, 1e-12);
}

TEST(ResidualBlockTest, InterfaceAndRotations) {
  auto weights = LoadWeights();
  auto profile = BootParameter::Func15RBoot();
  auto param = profile.MakeParameter();
  auto context = BootContext<uint64_t>::Create(*param, profile);
  context->encoder_.EnableNearestRounding();
  context->EnableCenteredMergedRescale();
  context->Prepare();
  const auto &exec =
      dynamic_cast<const Func15RBootExecution &>(context->GetRouteExecution());

  auto shape32 = TensorShape::Create(32, 16, profile.slots);
  auto shape16 = TensorShape::Create(16, 32, profile.slots);

  auto stage32 = std::make_shared<ConvStCStage>(context, shape32, profile);
  auto masks32 = stage32->CreateMaskPlaintexts();
  auto stage16 = std::make_shared<ConvStCStage>(context, shape16, profile);
  auto masks16 = stage16->CreateMaskPlaintexts();
  auto split32 = std::make_shared<SplitAndQuadruple>(context, shape32, profile);

  // Stride 1 Block (Stage 1 Block 0)
  ResidualBlock block_stride1(context, exec, profile, stage32, &masks32, nullptr,
                              shape32, shape32, 1,
                              weights.conv1[0], weights.conv2[0], "g1-b0");
  EXPECT_EQ(block_stride1.GetStride(), 1);
  EXPECT_EQ(block_stride1.GetName(), "g1-b0");
  EXPECT_EQ(block_stride1.GetInShape().channels, 16);
  EXPECT_EQ(block_stride1.GetOutShape().channels, 16);

  EvkRequest req1;
  block_stride1.AddRequiredRotations(req1);
  EXPECT_FALSE(req1.empty());

  // Stride 2 Block (Stage 2 Block 0 Transition)
  ResidualBlock block_stride2(context, exec, profile, stage16, &masks16, split32,
                              shape32, shape16, 2,
                              weights.conv1[3], weights.conv2[3], "g2-b0");
  EXPECT_EQ(block_stride2.GetStride(), 2);
  EXPECT_EQ(block_stride2.GetName(), "g2-b0");
  EXPECT_EQ(block_stride2.GetInShape().channels, 16);
  EXPECT_EQ(block_stride2.GetOutShape().channels, 32);

  EvkRequest req2;
  block_stride2.AddRequiredRotations(req2);
  EXPECT_FALSE(req2.empty());
}

size_t UsedVramBytes() {
  size_t free_b = 0, total_b = 0;
  cudaMemGetInfo(&free_b, &total_b);
  return total_b - free_b;
}

TEST(ResNet20CnnTest, EncryptedInferenceCifar10Index0) {
  auto weights = LoadWeights();
  int label = -1;
  auto img = LoadImage(label);

  auto profile = BootParameter::Func15RBoot();
  auto param = profile.MakeParameter();
  auto context = BootContext<uint64_t>::Create(*param, profile);
  context->encoder_.EnableNearestRounding();
  context->EnableCenteredMergedRescale();
  context->memory_pool_.EnableStatistics();
  context->Prepare();
  const auto &exec =
      dynamic_cast<const Func15RBootExecution &>(context->GetRouteExecution());

  UserInterface<uint64_t> client(context, SparseSecretLayout::kSubringPrefix,
                                 profile.extension, 3.2);

  auto prep_start = std::chrono::steady_clock::now();
  ResNet20Classifier classifier(context, exec, profile, weights);
  const double prep_s = std::chrono::duration<double>(
                            std::chrono::steady_clock::now() - prep_start)
                            .count();
  std::cout << "[PREPARE] ResNet20Classifier preparation: " << prep_s << " s"
            << std::endl;

  EvkRequest req;
  context->AddRequiredRotations(req, profile.slots);
  classifier.AddRequiredRotations(req);
  client.PrepareRotationKey(req);
  client.PrepareMEBootAKSKeys(context->GetMEBootCtS(profile.slots), true);

  // Encrypt the stem input (3 channels + zero channel) at Level 2.
  Tensor input_channels = img;
  input_channels.push_back(
      std::vector<std::vector<double>>(32, std::vector<double>(32, 0.0)));
  auto shape = ResNet20Classifier::InputShape();
  Plaintext<uint64_t> pt;
  context->encoder_.Encode(pt, 2, profile.application_scale,
                           PackChannels(input_channels, shape, profile.slots));
  Ciphertext<uint64_t> input;
  client.Encrypt(input, pt);

  // Verify ResidualBlock interface properties on constructed classifier
  for (int i = 0; i < ResNet20Classifier::kBlocks; ++i) {
    const auto &block = classifier.GetBlock(i);
    const int g = i / 3;
    const int b = i % 3;
    const int expected_stride = (b == 0 && g > 0) ? 2 : 1;
    EXPECT_EQ(block.GetStride(), expected_stride);
    EXPECT_FALSE(block.GetName().empty());
  }

  Ciphertext<uint64_t> logits_ct;
  ResNet20Classifier::InferenceTiming timing;
  classifier.Classify(logits_ct, input, client.GetEvkMap());  // warm-up
  const auto mem_before = context->memory_pool_.GetStatistics();
  const size_t vram_before = UsedVramBytes();

  cudaDeviceSynchronize();
  classifier.Classify(logits_ct, input, client.GetEvkMap(), &timing);
  cudaDeviceSynchronize();

  const size_t vram_after = UsedVramBytes();
  const auto mem_after = context->memory_pool_.GetStatistics();

  EXPECT_EQ(context->param_.NPToLevel(logits_ct.GetNP()), 1);
  Plaintext<uint64_t> dec;
  client.Decrypt(dec, logits_ct);
  std::vector<Complex> slots;
  context->encoder_.Decode(slots, dec);
  std::vector<double> logits(10);
  for (int i = 0; i < 10; ++i) logits[i] = slots[i].real();

  auto golden = GoldenLogits();
  double max_err = 0.0;
  for (int i = 0; i < 10; ++i) {
    max_err = std::max(max_err, std::abs(logits[i] - golden[i]));
    std::cout << "[LOGIT] " << i << ": HE " << logits[i] << " golden "
              << golden[i] << std::endl;
  }
  std::cout << "[ACCURACY] Encrypted ResNet-20 logits max err vs golden: "
            << max_err << " (tolerance < 0.01)" << std::endl;

  const double cpu_ref_s = 221.7;
  const double gpu_inf_s = timing.total_ms / 1000.0;
  const double speedup = cpu_ref_s / gpu_inf_s;

  std::cout << "\n=================== PROFILING BREAKDOWN ===================" << std::endl;
  std::cout << "Stem (Conv-StC + Refresh 1):        " << timing.stem_ms << " ms ("
            << (timing.stem_ms / timing.total_ms * 100.0) << "%)" << std::endl;
  std::cout << "Stage 1 (32x32, 6 Refreshes):       " << timing.stage1_ms << " ms ("
            << (timing.stage1_ms / timing.total_ms * 100.0) << "%)" << std::endl;
  std::cout << "Stage 2 (16x16, 7 Refreshes+Split): " << timing.stage2_ms << " ms ("
            << (timing.stage2_ms / timing.total_ms * 100.0) << "%)" << std::endl;
  std::cout << "Stage 3 (8x8,   7 Refreshes+Split): " << timing.stage3_ms << " ms ("
            << (timing.stage3_ms / timing.total_ms * 100.0) << "%)" << std::endl;
  std::cout << "AvgPool + FC Head:                  " << timing.pool_fc_ms << " ms ("
            << (timing.pool_fc_ms / timing.total_ms * 100.0) << "%)" << std::endl;
  std::cout << "-----------------------------------------------------------" << std::endl;
  std::cout << "Total Bootstraps (21 Refreshes):    " << timing.total_bootstrap_ms << " ms ("
            << (timing.total_bootstrap_ms / timing.total_ms * 100.0) << "%)" << std::endl;
  std::cout << "  - Activation Refreshes (x19):     " << timing.activation_refreshes << std::endl;
  std::cout << "  - Identity Refreshes (x2):        " << timing.identity_refreshes << std::endl;
  std::cout << "Total Linear Layers (Convs/Splits): " << timing.total_linear_ms << " ms ("
            << (timing.total_linear_ms / timing.total_ms * 100.0) << "%)" << std::endl;
  std::cout << "-----------------------------------------------------------" << std::endl;
  std::cout << "Total Encrypted GPU Inference Time: " << timing.total_ms << " ms ("
            << gpu_inf_s << " s)" << std::endl;
  std::cout << "CPU Native Baseline Reference Time: " << cpu_ref_s << " s" << std::endl;
  std::cout << "End-to-End Inference Speedup:       " << speedup << "x" << std::endl;
  std::cout << "===========================================================\n" << std::endl;

  std::cout << "[MEMORY] Pool peak allocated: " << mem_after.peak_bytes / 1048576
            << " MB, live before/after: " << mem_before.live_bytes / 1048576
            << " / " << mem_after.live_bytes / 1048576 << " MB" << std::endl;
  std::cout << "[MEMORY] System VRAM used before/after: " << vram_before / 1048576
            << " / " << vram_after / 1048576 << " MB" << std::endl;

  EXPECT_EQ(Argmax(logits), label);
  EXPECT_LT(max_err, 0.01);
  EXPECT_LT(mem_after.peak_bytes, int64_t(15385) * 1048576);
  // Repeated inference must not grow the resident footprint (leak check).
  EXPECT_EQ(mem_after.live_bytes, mem_before.live_bytes);
}

}  // namespace
