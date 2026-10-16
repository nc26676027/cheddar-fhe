# Cheddar FHE

Cheddar is a C++/CUDA CKKS library for GPU homomorphic evaluation. Its selected
64-bit, full-slot bootstrap uses native Cheddar operators. MEBOOT Identity19 is
the default route: a level-2 input at scale 2^40 returns at level 21 and scale
2^40. Ordinary is available as a comparison route and returns at level 20.

## Build

Requires an NVIDIA GPU, CUDA 11.8 or later, CMake 3.24 or later, libtommath,
and zlib when building the test suite. On Ubuntu, install build tools,
`libtommath-dev` and `zlib1g-dev`; install `libgmp-dev` if using
`-DUSE_GMP=ON`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_EXTENSION=ON
cmake --build build -j
```

`BUILD_UNITTEST=ON` is the default. Set it to `OFF` to build the library
without tests.

## Advanced bootstrap

Cheddar exposes advanced bootstrap routes through one native `BootContext`
dispatch. `Identity19` is the default full-slot MEBOOT refresh. The discrete
CKKS MEBOOT route uses the same Cheddar StC, entry gadget, centered lift,
MEBOOT CtS, NTT, key-switch, polynomial and rescale operators, then adds the
certified integer-cleaning and EvalRound finish.

The selected Identity19 contract is full packing (32768 slots), `uint64_t`,
input L2 at scale 2^40. The following shows the intended production call
sequence; ciphertext preparation and encryption use the existing Cheddar
client flow.

```cpp
auto boot_param = BootParameter::Identity19();
auto param = boot_param.MakeParameter();
auto context = BootContext<uint64_t>::Create(*param, boot_param);
context->Prepare();

EvkRequest req;
context->AddRequiredRotations(req, 32768);
UserInterface<uint64_t> client(context);
client.PrepareRotationKey(req);
client.PrepareMEBootAKSKeys(context->GetMEBootCtS(32768));

Ciphertext<uint64_t> output;
context->Boot(output, input, client.GetEvkMap());
```

For discrete CKKS MEBOOT, bind the certified extractor parameters and use the
same public `BootContext` seam:

```cpp
auto boot_param = BootParameter::DiscreteCKKSMeBoot();
auto param = boot_param.MakeParameter();
auto context = BootContext<uint64_t>::Create(
    *param, boot_param, discrete_identity_parameters);
context->Prepare();

EvkRequest req;
context->AddRequiredRotations(req, BootParameter::kSlots);
UserInterface<uint64_t> client(context);
client.PrepareRotationKey(req);
client.PrepareMEBootAKSKeys(context->GetMEBootCtS(BootParameter::kSlots));
context->Boot(output, input, client.GetEvkMap());
```

`BootParameter::Selected()` remains available for compatibility. Select
`BootRoute::kOrdinary` for the comparison route and omit AKS key preparation.
`ProfileBoot` measures whichever advanced route was prepared. The exact
contracts, qualification limits and historical baseline are in
[ADR-0008](docs/adr/0008-identity19-production-boot-seam.md) and the
[Identity19 verification report](docs/research/identity19-verification-20261015.md)
and [ADR-0010](docs/adr/0010-discrete-meboot-production-route.md). Fresh
implementation evidence is recorded in
`docs/evidence/identity19/` and `docs/evidence/discrete-identity/`.

## Contact

Please use GitHub issues for any suggestions.
For other inquiries, you can take a look at our [ASPLOS 2026 paper](https://doi.org/10.1145/3760250.3762223) (or [arXiv version](https://arxiv.org/abs/2407.13055)) or contact the authors by e-mail:
* Jongmin Kim (firstname.lastname@snu.ac.kr)
* Wonseok Choi (firstname.lastname@snu.ac.kr)

## License and Citing

See the [License](./LICENSE).
Cheddar (all the files in this repository) is licensed under the MIT License.

Cheddar dynamically links the following third-party libraries:
* NVIDIA CUDA Runtime library (cudart), which is provided under the NVIDIA CUDA Toolkit End User License Agreement:
https://docs.nvidia.com/cuda/eula/index.html
* RMM (licensed under the Apache 2.0 License): https://github.com/rapidsai/rmm
* libtommath (public domain software): https://github.com/libtom/libtommath
* GoogleTest (licensed under the BSD 3-Clause License): https://github.com/google/googletest
* JsonCpp (licensed under the MIT License / public domain software): https://github.com/open-source-parsers/jsoncpp
* GMP (licensed under the LGPL v3): https://gmplib.org/

When using Cheddar (or even Cheddar parameters in the [parameters folder](./parameters)), please cite the following paper:
```
@inproceedings{asplos-2026-cheddar,
  author = {Choi, Wonseok and Kim, Jongmin and Ahn, {Jung Ho}},
  title = {Cheddar: {A} Swift Fully Homomorphic Encryption Library Designed for {GPU} Architectures},
  booktitle = {Proceedings of the 31st ACM International Conference on Architectural Support for Programming Languages and Operating Systems},
  year = {2025},
  url = {https://doi.org/10.1145/3760250.3762223},
  doi = {10.1145/3760250.3762223}
}
```

## References

1. Samardzic, Nikola, and Daniel Sanchez. "Bitpacker: Enabling High Arithmetic Efficiency in Fully Homomorphic Encryption Accelerators." ACM International Conference on Architectural Support for Programming Languages and Operating Systems (ASPLOS). 2024. [Link](https://doi.org/10.1145/3620665.3640397)
2. Cheon, Jung Hee, et al. "Grafting: Complementing RNS in CKKS." IACR Cryptology ePrint Archive. 2024. [Link](https://eprint.iacr.org/2024/1014)
