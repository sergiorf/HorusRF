# HorusRF language reference

This document describes the currently implemented demonstration language. The
authoritative concrete syntax is
[`grammar/horusrf.ebnf`](../grammar/horusrf.ebnf); syntax changes must keep the
grammar, lexer, parser, this reference, and tests synchronized.

## Lexical rules and source positions

Whitespace separates tokens and `//` starts a comment through the end of its line.
Identifiers start with an ASCII letter and continue with ASCII letters, digits, or
underscores. Keywords are lowercase and unit spelling is exact. Decimal numbers
require a leading digit and, when a decimal point is present, digits after it;
scientific notation and leading-dot forms are rejected.

Punctuation is `{ } = .. . + - ( ) , %`. Units are `Hz`, `kHz`, `MHz`, `GHz`, `dBm`,
`dB`, `%`, `K`, `degC`, `deltaK`, and `deltaC`. Source locations are one-based line and
column numbers; spans use byte offsets internally.

## Program and declarations

A source file contains exactly one named `characterize` block. The canonical
legacy program is [`examples/tx_path_characterization.hrf`](../examples/tx_path_characterization.hrf):

```horusrf
characterize tx_path {
  reference power = -10 dBm
  sweep frequency 2.40 GHz .. 2.50 GHz step 1 MHz
  measure power
  derive calibration tx_power {
    correction = reference.power - power
    over frequency
  }
}
```

`reference power` declares the sole nominal comparison power. Its expression name
is `reference.power`. A program requires exactly one reference, one frequency
sweep, an optional temperature sweep, at least one power measurement, and one
calibration. Sweeps retain declaration order and must precede measurements. Duplicate
dimensions are errors; a missing required declaration is reported after statement
analysis.

Frequency endpoints and steps use frequency units. Temperature endpoints use `K`
or `degC`, while temperature steps must use the explicit delta units `deltaK` or
`deltaC`; an absolute Celsius unit is never accepted as a step. All steps are
strictly positive and an end must not precede its start. Axis counts use integer
index generation and the existing endpoint tolerance. Each axis and the checked
Cartesian product are limited to 1,000,000 points. At runtime the last declared
dimension varies fastest.

M1 compiles and lowers multidimensional programs and M2 executes their deterministic
Cartesian product. Sweep controls are applied in declaration order at every point,
followed by output power and measurements. A missing environmental controller is
reported as `runtime.capability_unavailable` before any device call. The fixed CSV
schema remains available only for a single frequency sweep; multidimensional
`--csv` requests produce `output.csv_unrepresentable` before execution. `measure
power` defines `power` and remains the legacy spelling. Named measurements use:

```horusrf
measure output_power = power
measure evm_result = evm
measure carrier_error = frequency_error
```

The operations return `Power`, `Ratio`, and `FrequencyDelta`, respectively. Each
measurement runs once per operating point in source order. Runtime capability
preflight checks every required public device interface before any equipment call.

Statements are analyzed in written order. `frequency`, `temperature`, `power`,
named measurements, `reference.power`, and derived names are usable only after their declarations.
Forward references, unknown names, duplicate user names, and attempts to reuse a
reserved sweep or measurement name are rejected.

## Expressions and quantities

Expressions contain quantities, identifiers, `reference.power`, parentheses, unary
minus, and binary `+` and `-`. Binary operators have equal precedence and associate
left to right. Unary minus binds more tightly. Quantities normalize as follows:

| Source unit | Semantic type | Canonical storage |
|---|---|---|
| `Hz`, `kHz`, `MHz`, `GHz` | `Frequency` | Hz |
| `dBm` | `Power` | dBm |
| `dB` | `PowerDelta` | dB |
| `%` | `Ratio` | ratio (source value divided by 100) |
| `K`, `degC` | `Temperature` | K (`degC` adds 273.15) |
| `deltaK`, `deltaC` | `TemperatureDelta` | K |

All converted values must be finite, and absolute temperatures below 0 K are
rejected. Celsius conversion is affine only for absolute temperature; delta units
have identical Kelvin-sized increments.

Only these arithmetic operations are supported:

| Expression | Result |
|---|---|
| `Power - Power` | `PowerDelta` |
| `Power + PowerDelta` | `Power` |
| `PowerDelta + PowerDelta` | `PowerDelta` |
| unary `-PowerDelta` | `PowerDelta` |
| `FrequencyDelta +/- FrequencyDelta` | `FrequencyDelta` |
| unary `-FrequencyDelta` | `FrequencyDelta` |
| `Ratio +/- Ratio` | `Ratio` |
| unary `-Ratio` | `Ratio` |
| `TemperatureDelta +/- TemperatureDelta` | `TemperatureDelta` |
| unary `-TemperatureDelta` | `TemperatureDelta` |

The language rejects `Power + Power`, `PowerDelta + Power`,
`PowerDelta - PowerDelta`, `PowerDelta - Power`, `Power - PowerDelta`, unary
`-Power`, and arithmetic involving absolute `Frequency`. Operand order is significant.

Results retain every observation's declaration name, strong type, and canonical
value. The fixed legacy CSV remains available only to the original single-frequency,
legacy-`measure power` shape; richer result serialization is introduced by M5.

## Calibration artifacts

```horusrf
derive calibration tx_power {
  correction = reference.power - power
  over frequency
}
```

The correction must have `PowerDelta` type. `over` is mandatory and names explicit
index dimensions. Every index must already name an active sweep. The list must
contain every active sweep exactly once and in declaration order; unknown,
non-sweep, duplicate, missing, and reordered indexes are rejected. A checked
two-dimensional program therefore declares `over frequency, temperature`.

The runner produces a standard verification report for every characterization.
For each point it calculates `error = measured - reference`, applies the declared
calibration as `corrected = measured + correction`, and calculates
`residual = corrected - reference`. These are runner-generated report fields, not
named DSL values, and source expressions cannot reference them. Runtime stores the
typed coordinate tuple and one correction entry per sample; there is no calibration
application statement in the language.

## Diagnostics

Source diagnostics use a stable lowercase code and the source span's beginning:

```text
experiment.hrf:2:11: parse.invalid_token: invalid token '@'
experiment.hrf:2:21: semantic.unexpected_quantity_type: expected Power, got PowerDelta
```

Parse, semantic, lowering, and runtime codes begin with `parse.`, `semantic.`,
`lowering.`, and `runtime.` respectively. A source diagnostic exits the CLI with
status 4, writes to standard error, emits no success output, and does not create a
requested CSV. Usage errors exit 2 and file I/O errors exit 3.

`horusrf-run <source.hrf> --check` performs parsing, semantic analysis, and lowering
without constructing devices or executing the characterization. A successful check
is silent and exits with status 0. `--check` and `--csv` are mutually exclusive.

Examples of rejected programs include a missing unit (`reference power = 10`), an
unknown name (`derive x = missing`), a non-positive step, and a calibration indexed
over `power` rather than `frequency`.

## Current limitations

The demonstration has no conditionals, general loops, functions, arrays, strings,
interpolation, multiple characterizations, phase/gain dimensions, instrument
declarations, procedural DSL statements,
calibration import or persistence, or explicit calibration application. These
omissions define the current scope.
