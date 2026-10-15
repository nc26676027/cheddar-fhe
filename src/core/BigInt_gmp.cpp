#include "common/Assert.h"
#include "core/BigInt.h"

#include <stdexcept>

namespace {
bool IsDecimalInteger(std::string_view value) {
  if (value.empty()) return false;
  size_t pos = value.front() == '-' ? 1 : 0;
  if (pos == value.size()) return false;
  for (; pos < value.size(); ++pos)
    if (value[pos] < '0' || value[pos] > '9') return false;
  return true;
}
}  // namespace

#define CHECK_GMP_ERROR(name, expr) \
  do {                              \
    try {                           \
      expr;                         \
    } catch (...) {                 \
      std::string __err_msg = name; \
      __err_msg += " failed";       \
      Fail(__err_msg);              \
    }                               \
  } while (0)

namespace cheddar {

BigInt::BigInt(uint64_t value) {
  mpz_init(data_);
  mpz_set_ui(data_, value);
}

BigInt::BigInt(double value) {
  mpz_init(data_);
  mpz_set_d(data_, value);
}

BigInt::BigInt(std::string_view decimal) {
  if (!IsDecimalInteger(decimal))
    throw std::invalid_argument("BigInt requires a decimal integer");
  mpz_init(data_);
  if (mpz_set_str(data_, std::string(decimal).c_str(), 10) != 0) {
    mpz_clear(data_);
    throw std::runtime_error("BigInt decimal parse failed");
  }
}

BigInt::BigInt(const BigInt &other) {
  mpz_init(data_);
  mpz_set(data_, other.data_);
}

BigInt &BigInt::operator=(const BigInt &other) {
  if (this != &other) {
    mpz_set(data_, other.data_);
  }
  return *this;
}

BigInt::~BigInt() { mpz_clear(data_); }

void BigInt::Set(uint64_t value) { mpz_set_ui(data_, value); }

uint64_t BigInt::GetUnsigned() const {
  AssertFalse(mpz_sgn(data_) < 0, "BigInt::GetUnsigned: negative value");
  return mpz_get_ui(data_);
}

double BigInt::GetDouble() const { return mpz_get_d(data_); }
std::string BigInt::ToString() const {
  std::string buffer(mpz_sizeinbase(data_, 10) + 3, '\0');
  mpz_get_str(buffer.data(), 10, data_);
  buffer.resize(std::char_traits<char>::length(buffer.c_str()));
  return buffer;
}

// result = -op
void BigInt::Neg(BigInt &result, const BigInt &op) {
  mpz_neg(result.data_, op.data_);
}

// result = op1 + op2
void BigInt::Add(BigInt &result, const BigInt &op1, const BigInt &op2) {
  mpz_add(result.data_, op1.data_, op2.data_);
}

// result = op1 - op2
void BigInt::Sub(BigInt &result, const BigInt &op1, const BigInt &op2) {
  mpz_sub(result.data_, op1.data_, op2.data_);
}

// result = op1 * op2
void BigInt::Mult(BigInt &result, const BigInt &op1, const BigInt &op2) {
  mpz_mul(result.data_, op1.data_, op2.data_);
}

// --- more complex operations ---

// result = op >> 1
void BigInt::Div2(BigInt &result, const BigInt &op) {
  AssertFalse(mpz_sgn(op.data_) < 0, "BigInt::Div2: negative value");
  mpz_fdiv_q_2exp(result.data_, op.data_, 1);
}

// result = op1 / op2
void BigInt::Div(BigInt &result, const BigInt &op1, const BigInt &op2) {
  mpz_tdiv_q(result.data_, op1.data_, op2.data_);
}

// result = op % mod. The result is always positive.
void BigInt::Mod(BigInt &result, const BigInt &op, const BigInt &mod) {
  mpz_mod(result.data_, op.data_, mod.data_);
}

// result = op % mod. The result is in range [- (mod - 1) / 2, (mod - 1) / 2]
void BigInt::NormalizeMod(BigInt &result, const BigInt &op, const BigInt &mod,
                          const BigInt &half_mod) {
  mpz_mod(result.data_, op.data_, mod.data_);
  if (mpz_cmp(result.data_, half_mod.data_) > 0) {  // result > half_mod
    mpz_sub(result.data_, result.data_, mod.data_);
  }
}

}  // namespace cheddar
