package main

import (
	"encoding/json"
	"os"

	"github.com/tuneinsight/lattigo/v6/core/rlwe"
	"paper1-meboot/identity"
	"paper1-meboot/identity/presets"
)

func main() {
	out := map[string]any{}
	for _, item := range []struct {
		name  string
		route identity.Route
	}{{"joint4434", identity.RouteJoint4434}, {"ordinary", identity.RouteOrdinary}} {
		p, err := presets.Identity16Selected(item.route)
		if err != nil {
			panic(err)
		}
		main := p.Parameters.BootstrappingParameters
		coefficients := make([][2]float64, len(p.Polynomial.Mod1Poly.Coeffs))
		for i, coefficient := range p.Polynomial.Mod1Poly.Coeffs {
			if coefficient != nil {
				value := coefficient.Complex128()
				coefficients[i] = [2]float64{real(value), imag(value)}
			}
		}
		record := map[string]any{
			"q": main.Q(), "p": main.P(),
			"residual_q": p.Parameters.ResidualParameters.Q(),
			"mod1": p.Parameters.Mod1ParametersLiteral,
			"mod1_coefficients_float64": coefficients,
			"mod1_q_diff": p.Polynomial.QDiff,
			"mod1_actual_degree": p.Polynomial.Mod1Poly.Degree(),
			"mod1_interval_a": p.Polynomial.Mod1Poly.A.Text('g', 30),
			"mod1_interval_b": p.Polynomial.Mod1Poly.B.Text('g', 30),
			"mod1_sqrt2pi": p.Polynomial.Sqrt2Pi,
			"mod1_power_basis_scale_lift": p.Mod1PowerBasisScaleLift,
			"cts": p.Parameters.CoeffsToSlotsParameters,
			"stc": p.Parameters.SlotsToCoeffsParameters,
			"stc_output_scale_log2_override": p.StCOutputScaleLog2,
			"stc_intermediate_scale_log2_override": p.StCIntermediateScaleLog2,
			"ordinary_widths": p.OrdinaryWidths, "ordinary_bases": p.OrdinaryBases,
			"joint_design": p.Design,
			"subring_extension": p.Parameters.SubringSecretExtDegree,
			"subring_h": p.Parameters.SubringEphemeralSecretWeight,
			"ephemeral_h": p.Parameters.EphemeralSecretWeight,
			"entry_q0_override": p.SubringAuxiliaryQ0,
			"entry_p_override": p.SubringAuxiliaryP,
			"entry_digit_bits": p.SubringAuxiliaryBaseTwoBits,
			"exact_log2_qp": main.LogQP(),
		}
		if item.name == "joint4434" {
			extra, _, err := rlwe.GenModuli(main.LogNthRoot(), []int{53, 53}, nil)
			if err != nil {
				panic(err)
			}
			for _, candidate := range extra {
				found := false
				for _, q := range main.Q() {
					found = found || candidate == q
				}
				if !found {
					record["proper_extra_q"] = candidate
					break
				}
			}
		}
		out[item.name] = record
	}
	encoder := json.NewEncoder(os.Stdout)
	encoder.SetIndent("", "  ")
	if err := encoder.Encode(out); err != nil {
		panic(err)
	}
}
