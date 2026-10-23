#include <gtest/gtest.h>

#include <cmath>
#include <fstream>
#include <iostream>
#include <vector>

#include "UserInterface.h"
#include "core/Context.h"
#include "extension/BootContext.h"
#include "extension/BootParameter.h"
#include "extension/cnn/AveragePooling.h"
#include "extension/cnn/ConvStCFusion.h"
#include "extension/cnn/FullyConnected.h"
#include "extension/cnn/TensorShape.h"
#include "nlohmann/json.hpp"

using namespace cheddar;
using namespace cheddar::cnn;

namespace {

std::string FindFixturePath(const std::string &filename) {
  std::vector<std::string> candidates = {
      filename,
      "unittest/fixtures/" + filename,
      "../unittest/fixtures/" + filename,
      "../../unittest/fixtures/" + filename,
  };
  for (const auto &p : candidates) {
    std::ifstream f(p);
    if (f.good()) return p;
  }
  return filename;
}

std::vector<std::vector<std::vector<double>>> Conv2DReference(
    const std::vector<std::vector<std::vector<double>>> &input,
    const ConvLayer &layer, int padding = 1) {
  int in_ch = layer.in_ch;
  int out_ch = layer.out_ch;
  int stride = layer.stride;
  int h = input[0].size();
  int w = input[0][0].size();
  int out_h = (h + 2 * padding - 3) / stride + 1;
  int out_w = (w + 2 * padding - 3) / stride + 1;

  std::vector<std::vector<std::vector<double>>> out(
      out_ch, std::vector<std::vector<double>>(out_h, std::vector<double>(out_w, 0.0)));

  for (int oc = 0; oc < out_ch; ++oc) {
    for (int ic = 0; ic < in_ch; ++ic) {
      for (int i = 0; i < out_h; ++i) {
        for (int j = 0; j < out_w; ++j) {
          double sum = 0.0;
          for (int kh = 0; kh < 3; ++kh) {
            for (int kw = 0; kw < 3; ++kw) {
              int r = i * stride + kh - padding;
              int c = j * stride + kw - padding;
              if (r >= 0 && r < h && c >= 0 && c < w) {
                sum += input[ic][r][c] * layer.weight[oc][ic][kh][kw];
              }
            }
          }
          out[oc][i][j] += sum;
        }
      }
    }
    for (int i = 0; i < out_h; ++i) {
      for (int j = 0; j < out_w; ++j) {
        out[oc][i][j] += layer.bias[oc];
      }
    }
  }
  return out;
}

TEST(ConvStCFusionTest, TensorShapePackingAndUnpacking) {
  auto shape32 = TensorShape::Create(32, 16, 16384);
  EXPECT_EQ(shape32.tensor_wh, 32);
  EXPECT_EQ(shape32.log_n1, 10);
  EXPECT_EQ(shape32.chan_packing, 16);
  EXPECT_EQ(shape32.chan_groups, 1);

  auto shape16 = TensorShape::Create(16, 32, 16384);
  EXPECT_EQ(shape16.tensor_wh, 16);
  EXPECT_EQ(shape16.log_n1, 8);
  EXPECT_EQ(shape16.chan_packing, 64);
  EXPECT_EQ(shape16.chan_groups, 1);

  auto shape8 = TensorShape::Create(8, 64, 16384);
  EXPECT_EQ(shape8.tensor_wh, 8);
  EXPECT_EQ(shape8.log_n1, 6);
  EXPECT_EQ(shape8.chan_packing, 256);
  EXPECT_EQ(shape8.chan_groups, 1);

  std::vector<std::vector<std::vector<double>>> test_channels(
      16, std::vector<std::vector<double>>(32, std::vector<double>(32, 0.0)));
  for (int c = 0; c < 16; ++c) {
    for (int i = 0; i < 32; ++i) {
      for (int j = 0; j < 32; ++j) {
        test_channels[c][i][j] = (c * 100 + i * 32 + j) * 0.001;
      }
    }
  }

  auto packed = PackChannels(test_channels, shape32, 16384);
  EXPECT_EQ(packed.size(), 16384u);

  auto unpacked = UnpackChannels(packed, shape32);
  ASSERT_EQ(unpacked.size(), 16u);

  double max_diff = 0.0;
  for (int c = 0; c < 16; ++c) {
    for (int i = 0; i < 32; ++i) {
      for (int j = 0; j < 32; ++j) {
        max_diff = std::max(max_diff, std::abs(unpacked[c][i][j] - test_channels[c][i][j]));
      }
    }
  }
  EXPECT_LT(max_diff, 1e-12);
}

TEST(ConvStCFusionTest, StemFusedConvStCPlaintextReference) {
  // 1. Load fixtures
  std::ifstream model_file(FindFixturePath("resnet20_folded.json"));
  ASSERT_TRUE(model_file.is_open()) << "Cannot open resnet20_folded.json";
  nlohmann::json model_json;
  model_file >> model_json;
  const auto &stem_json = model_json["stem"];

  ConvLayer stem_layer;
  stem_layer.in_ch = stem_json["in_ch"].get<int>();
  stem_layer.out_ch = stem_json["out_ch"].get<int>();
  stem_layer.stride = stem_json["stride"].get<int>();
  stem_layer.bias = stem_json["bias"].get<std::vector<double>>();

  auto raw_weights = stem_json["weight"].get<std::vector<double>>();
  stem_layer.weight.assign(
      stem_layer.out_ch,
      std::vector<std::vector<std::vector<double>>>(
          stem_layer.in_ch,
          std::vector<std::vector<double>>(3, std::vector<double>(3, 0.0))));

  size_t w_idx = 0;
  for (int oc = 0; oc < stem_layer.out_ch; ++oc) {
    for (int ic = 0; ic < stem_layer.in_ch; ++ic) {
      for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
          stem_layer.weight[oc][ic][r][c] = raw_weights.at(w_idx++);
        }
      }
    }
  }

  std::ifstream img_file(FindFixturePath("cifar10_index0.json"));
  ASSERT_TRUE(img_file.is_open()) << "Cannot open cifar10_index0.json";
  nlohmann::json img_json;
  img_file >> img_json;
  auto img_raw = img_json["values"].get<std::vector<double>>();
  std::vector<std::vector<std::vector<double>>> input_img(
      3, std::vector<std::vector<double>>(32, std::vector<double>(32, 0.0)));
  size_t p_idx = 0;
  for (int c = 0; c < 3; ++c) {
    for (int i = 0; i < 32; ++i) {
      for (int j = 0; j < 32; ++j) {
        input_img[c][i][j] = img_raw.at(p_idx++);
      }
    }
  }

  // 2. Reference Conv2D + bias calculation
  auto want_pre = Conv2DReference(input_img, stem_layer, 1);

  // 3. Setup CKKS context and client
  auto profile = BootParameter::Func15RBoot();
  auto param = profile.MakeParameter();
  auto context = BootContext<uint64_t>::Create(*param, profile);
  context->encoder_.EnableNearestRounding();
  context->EnableCenteredMergedRescale();
  context->Prepare();

  UserInterface<uint64_t> client(
      context, SparseSecretLayout::kSubringPrefix, profile.extension, 3.2);

  auto shape32 = TensorShape::Create(32, 16, 16384);
  ConvStCStage stage(context, shape32, profile);

  // 4. Build fused kernel transform and bias plaintext
  auto fused_kernel = stage.CreateFusedKernelTransform(stem_layer);
  auto bias_pt = stage.CreateBiasPlaintext(stem_layer.bias);

  // 5. Prepare required keys
  EvkRequest req;
  stage.AddRequiredRotations(req);
  fused_kernel.AddRequiredRotations(req);
  client.PrepareRotationKey(req);

  // 6. Pack input channels (3 image channels + 1 zero channel) into Level 2 ciphertext
  std::vector<std::vector<std::vector<double>>> input_channels(4);
  input_channels[0] = input_img[0];
  input_channels[1] = input_img[1];
  input_channels[2] = input_img[2];
  input_channels[3].assign(32, std::vector<double>(32, 0.0));

  auto packed_slots = PackChannels(input_channels, shape32, 16384);
  Plaintext<uint64_t> input_pt;
  context->encoder_.Encode(input_pt, 2, profile.application_scale, packed_slots);
  Ciphertext<uint64_t> input_ct;
  client.Encrypt(input_ct, input_pt);

  // 7. Evaluate ConvStC on GPU
  Ciphertext<uint64_t> stc_out;
  stage.ConvStC(stc_out, input_ct, fused_kernel, &bias_pt, nullptr, nullptr,
                client.GetEvkMap());

  EXPECT_EQ(context->param_.NPToLevel(stc_out.GetNP()), 0);
  EXPECT_DOUBLE_EQ(stc_out.GetScale(), std::exp2(31.0));

  // 8. Decrypt and recover physical pre-activations
  Plaintext<uint64_t> dec_stc;
  client.Decrypt(dec_stc, stc_out);

  DeviceVector<uint64_t> coeffs(context->param_.degree_);
  auto view = coeffs.View();
  context->ntt_handler_.INTT(view, dec_stc.GetNP(), dec_stc.ConstView(), true);
  std::vector<uint64_t> h_coeffs(context->param_.degree_);
  cudaMemcpy(h_coeffs.data(), coeffs.data(),
             h_coeffs.size() * sizeof(uint64_t), cudaMemcpyDeviceToHost);

  uint64_t q0 = profile.q[0];
  double scale0 = std::exp2(31.0);
  std::vector<Complex> fold(16384);
  for (size_t i = 0; i < 16384; ++i) {
    int64_t real_centered = h_coeffs[i] >= (q0 / 2)
                                ? static_cast<int64_t>(h_coeffs[i]) - static_cast<int64_t>(q0)
                                : static_cast<int64_t>(h_coeffs[i]);
    int64_t imag_centered = h_coeffs[i + 16384] >= (q0 / 2)
                                ? static_cast<int64_t>(h_coeffs[i + 16384]) - static_cast<int64_t>(q0)
                                : static_cast<int64_t>(h_coeffs[i + 16384]);
    fold[i] = Complex(static_cast<double>(real_centered) / scale0,
                      static_cast<double>(imag_centered) / scale0);
  }

  auto unbrv = UnbrvSlots(fold, 14);
  auto actual_pre = UnpackChannels(unbrv, shape32);

  double max_err = 0.0;
  for (int c = 0; c < 16; ++c) {
    for (int i = 0; i < 32; ++i) {
      for (int j = 0; j < 32; ++j) {
        double got = actual_pre[c][i][j] * 15.0;  // 1/15 factor
        double want = want_pre[c][i][j];
        max_err = std::max(max_err, std::abs(got - want));
      }
    }
  }

  std::cout << "[ACCURACY] Stem Conv-StC Max Error vs Conv2D: " << max_err
            << " (spec tolerance: < 0.001)" << std::endl;
  EXPECT_LT(max_err, 0.001);
}

TEST(ConvStCFusionTest, ShortcutStCAndResidualAddition) {
  auto profile = BootParameter::Func15RBoot();
  auto param = profile.MakeParameter();
  auto context = BootContext<uint64_t>::Create(*param, profile);
  context->encoder_.EnableNearestRounding();
  context->EnableCenteredMergedRescale();
  context->Prepare();

  UserInterface<uint64_t> client(
      context, SparseSecretLayout::kSubringPrefix, profile.extension, 3.2);

  auto shape32 = TensorShape::Create(32, 16, 16384);
  ConvStCStage stage(context, shape32, profile);

  EvkRequest req;
  stage.AddRequiredRotations(req);
  client.PrepareRotationKey(req);

  // 1. Create two test inputs at Level 2 (e.g. simulated conv branch and shortcut branch)
  std::vector<std::vector<std::vector<double>>> channels_a(
      16, std::vector<std::vector<double>>(32, std::vector<double>(32, 0.5)));
  std::vector<std::vector<std::vector<double>>> channels_b(
      16, std::vector<std::vector<double>>(32, std::vector<double>(32, 0.25)));

  auto packed_a = PackChannels(channels_a, shape32, 16384);
  auto packed_b = PackChannels(channels_b, shape32, 16384);

  Plaintext<uint64_t> pt_a, pt_b;
  context->encoder_.Encode(pt_a, 2, profile.application_scale, packed_a);
  context->encoder_.Encode(pt_b, 2, profile.application_scale, packed_b);

  Ciphertext<uint64_t> ct_a, ct_b;
  client.Encrypt(ct_a, pt_a);
  client.Encrypt(ct_b, pt_b);

  // 2. Run ShortcutStC on ct_b
  Ciphertext<uint64_t> stc_shortcut;
  stage.ShortcutStC(stc_shortcut, ct_b, client.GetEvkMap());
  EXPECT_EQ(context->param_.NPToLevel(stc_shortcut.GetNP()), 0);
  EXPECT_DOUBLE_EQ(stc_shortcut.GetScale(), std::exp2(31.0));

  // 3. Also run ShortcutStC on ct_a to get a matching Level 0 ciphertext
  Ciphertext<uint64_t> stc_a;
  stage.ShortcutStC(stc_a, ct_a, client.GetEvkMap());

  // 4. Add them at Level 0
  Ciphertext<uint64_t> residual_sum;
  context->Add(residual_sum, stc_a, stc_shortcut);
  EXPECT_EQ(context->param_.NPToLevel(residual_sum.GetNP()), 0);
  EXPECT_DOUBLE_EQ(residual_sum.GetScale(), std::exp2(31.0));

  // 5. Decrypt and check sum: should equal (0.5 + 0.25) / 15.0 = 0.75 / 15.0 = 0.05
  Plaintext<uint64_t> dec_sum;
  client.Decrypt(dec_sum, residual_sum);

  DeviceVector<uint64_t> coeffs(context->param_.degree_);
  auto view = coeffs.View();
  context->ntt_handler_.INTT(view, dec_sum.GetNP(), dec_sum.ConstView(), true);
  std::vector<uint64_t> h_coeffs(context->param_.degree_);
  cudaMemcpy(h_coeffs.data(), coeffs.data(),
             h_coeffs.size() * sizeof(uint64_t), cudaMemcpyDeviceToHost);

  uint64_t q0 = profile.q[0];
  double scale0 = std::exp2(31.0);
  std::vector<Complex> fold(16384);
  for (size_t i = 0; i < 16384; ++i) {
    int64_t real_centered = h_coeffs[i] >= (q0 / 2)
                                ? static_cast<int64_t>(h_coeffs[i]) - static_cast<int64_t>(q0)
                                : static_cast<int64_t>(h_coeffs[i]);
    int64_t imag_centered = h_coeffs[i + 16384] >= (q0 / 2)
                                ? static_cast<int64_t>(h_coeffs[i + 16384]) - static_cast<int64_t>(q0)
                                : static_cast<int64_t>(h_coeffs[i + 16384]);
    fold[i] = Complex(static_cast<double>(real_centered) / scale0,
                      static_cast<double>(imag_centered) / scale0);
  }

  auto unbrv = UnbrvSlots(fold, 14);
  auto actual_sum = UnpackChannels(unbrv, shape32);

  double max_err = 0.0;
  for (int c = 0; c < 16; ++c) {
    for (int i = 0; i < 32; ++i) {
      for (int j = 0; j < 32; ++j) {
        double got = actual_sum[c][i][j] * 15.0;  // multiplied back by 15
        double want = 0.75;
        max_err = std::max(max_err, std::abs(got - want));
      }
    }
  }

  std::cout << "[ACCURACY] Shortcut StC & Residual Addition Max Error: "
            << max_err << " (spec tolerance: < 0.001)" << std::endl;
  EXPECT_LT(max_err, 0.001);
}

TEST(ConvStCFusionTest, AveragePoolingOperator) {
  auto profile = BootParameter::Func15RBoot();
  auto param = profile.MakeParameter();
  auto context = BootContext<uint64_t>::Create(*param, profile);
  context->encoder_.EnableNearestRounding();
  context->EnableCenteredMergedRescale();
  context->Prepare();

  UserInterface<uint64_t> client(
      context, SparseSecretLayout::kSubringPrefix, profile.extension, 3.2);

  AveragePooling pooling(context, profile);

  EvkRequest req;
  pooling.AddRequiredRotations(req);
  client.PrepareRotationKey(req);

  // Create known 8x8 x 64 channels
  const int num_channels = 64;
  std::vector<std::vector<std::vector<double>>> channels(
      num_channels, std::vector<std::vector<double>>(8, std::vector<double>(8, 0.0)));
  std::vector<double> expected_means(num_channels, 0.0);

  for (int c = 0; c < num_channels; ++c) {
    double sum = 0.0;
    for (int i = 0; i < 8; ++i) {
      for (int j = 0; j < 8; ++j) {
        double val = (c * 10.0 + i * 8 + j) * 0.01;
        channels[c][i][j] = val;
        sum += val;
      }
    }
    expected_means[c] = sum / 64.0;
  }

  auto shape8 = TensorShape::Create(8, 64, 16384);
  auto packed = PackChannels(channels, shape8, 16384);

  Plaintext<uint64_t> pt;
  context->encoder_.Encode(pt, 3, profile.application_scale, packed);
  Ciphertext<uint64_t> ct;
  client.Encrypt(ct, pt);

  Ciphertext<uint64_t> pooled;
  pooling.Pool(pooled, ct, client.GetEvkMap());

  EXPECT_EQ(context->param_.NPToLevel(pooled.GetNP()), 2);
  EXPECT_DOUBLE_EQ(pooled.GetScale(), profile.application_scale);

  // Decrypt and check slots c * 64
  Plaintext<uint64_t> dec_pt;
  client.Decrypt(dec_pt, pooled);
  std::vector<Complex> dec_slots(16384);
  context->encoder_.Decode(dec_slots, dec_pt);

  double max_err = 0.0;
  for (int c = 0; c < num_channels; ++c) {
    double got = dec_slots[c * 64].real();
    double want = expected_means[c];
    max_err = std::max(max_err, std::abs(got - want));
  }

  std::cout << "[ACCURACY] Average Pooling Max Error vs Float64 Mean: "
            << max_err << " (spec tolerance: < 0.001)" << std::endl;
  EXPECT_LT(max_err, 0.001);
}

TEST(ConvStCFusionTest, FullyConnectedClassificationHead) {
  std::ifstream model_file(FindFixturePath("resnet20_folded.json"));
  ASSERT_TRUE(model_file.is_open()) << "Cannot open resnet20_folded.json";
  nlohmann::json model_json;
  model_file >> model_json;
  const auto &fc_json = model_json["fc"];
  auto fc_weights = fc_json["weight"].get<std::vector<double>>();
  auto fc_bias = fc_json["bias"].get<std::vector<double>>();

  auto profile = BootParameter::Func15RBoot();
  auto param = profile.MakeParameter();
  auto context = BootContext<uint64_t>::Create(*param, profile);
  context->encoder_.EnableNearestRounding();
  context->EnableCenteredMergedRescale();
  context->Prepare();

  UserInterface<uint64_t> client(
      context, SparseSecretLayout::kSubringPrefix, profile.extension, 3.2);

  FullyConnected fc(context, profile, fc_weights, fc_bias);

  EvkRequest req;
  fc.AddRequiredRotations(req);
  client.PrepareRotationKey(req);

  // Create known 64 features at slots c * 64
  std::vector<Complex> pooled_slots(16384, Complex(0.0, 0.0));
  std::vector<double> features(64);
  for (int c = 0; c < 64; ++c) {
    features[c] = (c - 32) * 0.1;
    pooled_slots[c * 64] = Complex(features[c], 0.0);
  }

  // Calculate reference logits: W * features + b
  std::vector<double> want_logits(10, 0.0);
  for (int out = 0; out < 10; ++out) {
    double sum = 0.0;
    for (int c = 0; c < 64; ++c) {
      sum += fc_weights[out * 64 + c] * features[c];
    }
    want_logits[out] = sum + fc_bias[out];
  }

  Plaintext<uint64_t> pooled_pt;
  context->encoder_.Encode(pooled_pt, 2, profile.application_scale, pooled_slots);
  Ciphertext<uint64_t> pooled_ct;
  client.Encrypt(pooled_ct, pooled_pt);

  Ciphertext<uint64_t> logits_ct;
  fc.Classify(logits_ct, pooled_ct, client.GetEvkMap());

  EXPECT_EQ(context->param_.NPToLevel(logits_ct.GetNP()), 1);
  EXPECT_DOUBLE_EQ(logits_ct.GetScale(), profile.application_scale);

  Plaintext<uint64_t> dec_pt;
  client.Decrypt(dec_pt, logits_ct);
  std::vector<Complex> dec_slots(16384);
  context->encoder_.Decode(dec_slots, dec_pt);

  double max_err = 0.0;
  for (int out = 0; out < 10; ++out) {
    double got = dec_slots[out].real();
    double want = want_logits[out];
    max_err = std::max(max_err, std::abs(got - want));
  }

  std::cout << "[ACCURACY] Fully Connected Classification Max Error: "
            << max_err << " (spec tolerance: < 0.001)" << std::endl;
  EXPECT_LT(max_err, 0.001);
}

}  // namespace
