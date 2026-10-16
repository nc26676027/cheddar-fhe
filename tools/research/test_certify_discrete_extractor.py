"""Check the argument-map binding used by the exact continuum certificate."""

import json
import hashlib
from pathlib import Path
import unittest

from certify_discrete_extractor import certify


ROOT = Path(__file__).resolve().parents[2]


class ArgumentMapCertificateTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.raw = (ROOT / "parameters/research/discrete_direct6_k21_remez_d63.json").read_bytes()

    def test_remez_binary64_coefficients_cover_all_cells(self):
        certificate = certify(self.raw)
        self.assertTrue(certificate["extraction_contract_passed"])
        self.assertEqual(len(certificate["cell_bounds"]), 44)
        self.assertEqual(certificate["cell_bounds"][0]["integer"], -22)
        self.assertEqual(certificate["cell_bounds"][-1]["integer"], 21)
        self.assertEqual(certificate["argument_radius_rational"], "0x1/0x15800")

    def test_reject_mismatched_coordinate_encoding(self):
        manifest = json.loads(self.raw)
        manifest["argument_map"]["encoded_affine_scale"] = 1024
        with self.assertRaisesRegex(ValueError, "inconsistent affine encoding"):
            certify(json.dumps(manifest).encode())

    def test_evidence_is_bound_to_current_manifest(self):
        certificate = json.loads(
            (ROOT / "docs/evidence/discrete-identity/remez-20261016/direct6-certificate.json").read_bytes())
        self.assertEqual(certificate["manifest_sha256"],
                         hashlib.sha256(self.raw).hexdigest())
        self.assertTrue(certificate["extraction_contract_passed"])

    def test_oracle_fixture_uses_current_manifest(self):
        fixture = json.loads(
            (ROOT / "unittest/fixtures/discrete-direct6-remez-known-answers.json").read_bytes())
        self.assertEqual(fixture["manifest"], "discrete_direct6_k21_remez_d63.json")
        self.assertEqual(fixture["manifest_sha256"], hashlib.sha256(self.raw).hexdigest())
        self.assertEqual(len(fixture["rows"]), 44 * 3)

    def test_old_coordinate_contract_remains_certifiable(self):
        certificate = certify(
            (ROOT / "parameters/research/discrete_direct5_d47.json").read_bytes())
        self.assertTrue(certificate["extraction_contract_passed"])
        self.assertEqual(len(certificate["cell_bounds"]), 32)


if __name__ == "__main__":
    unittest.main()
