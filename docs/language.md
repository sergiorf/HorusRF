# HorusRF language reference

This document describes the currently implemented demonstration language. The
authoritative concrete syntax is the ANTLR grammar
[`grammar/HorusRf.g4`](../grammar/HorusRf.g4). Syntax changes must keep the grammar,
production frontend behavior, this reference, and tests synchronized.
The public parser remains routed through the deliberately small recursive-descent
implementation until the migration's parity and cutover gates pass. A private
generated frontend now constructs the same AST and structured diagnostics; its
[architecture decision](adr/0001-retain-recursive-descent-parser.md) documents
the grammar synchronization and conformance strategy.

## Lexical rules and source positions

ASCII whitespace is ignored and `//` starts a comment through the next line-feed
or end of file. Identifiers start with an ASCII letter and continue with ASCII
letters, digits, or underscores. Tokens use longest matching, are case-sensitive,
and cannot contain non-ASCII letters. Decimal numbers require a leading digit and,
when a decimal point is present, digits after it; signs are separate tokens.
Scientific notation, leading-dot forms, trailing-dot forms, and a number joined
directly to a word such as `1MHz` are rejected.

The reserved words are `characterize`, `reference`, `power`, `sweep`, `frequency`,
`temperature`, `step`, `measure`, `derive`, `calibration`, `correction`, `over`, and
`assert`. Unit spellings are also dedicated tokens and cannot be identifiers.
`evm`, `frequency_error`, and `abs` are ordinary identifier tokens whose meaning is
resolved during semantic analysis. For compatibility, the parser permits the
reserved words `power`, `frequency`, and `temperature` wherever an expression name,
measurement name/operation, reference member, or calibration index is expected.
Other reserved words are not valid names in those positions. Declaration sites
such as characterization, derived-value, and calibration names require an ordinary
identifier.

Punctuation is `{ } = .. . + - ( ) , % < <= > >= == !=`. There are no semicolons
or other statement terminators. A lone `!` is invalid.
Units are `Hz`, `kHz`, `MHz`, `GHz`, `dBm`,
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
minus, binary `+` and `-`, the built-in `abs()` function, and one optional comparison.
Arithmetic operators have equal precedence and associate left to right; unary minus
binds more tightly, and a comparison has lower precedence. Comparisons do not chain.
The complete precedence and associativity contract, from tightest to loosest, is:

| Form | Associativity |
|---|---|
| quantity, name, reference, one-argument call, parenthesized expression | n/a |
| unary `-` | prefix; one occurrence per unary expression |
| binary `+`, `-` | left |
| `<`, `<=`, `>`, `>=`, `==`, `!=` | non-associative; at most one |

Quantities normalize as follows:

| Source unit | Semantic type | Canonical storage |
|---|---|---|
| `Hz`, `kHz`, `MHz`, `GHz` | `Frequency` (or contextual `FrequencyDelta`) | Hz |
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

Results retain every observation's declaration name, operation, strong type, and
canonical value. The fixed legacy CSV remains available only to the original
single-frequency, legacy-`measure power` shape. `--json` writes the complete
versioned structured result described below.

## Assertions and qualification

An assertion is a per-operating-point specification result:

```horusrf
derive power_error = output_power - reference.power
assert abs(power_error) <= 1.5 dB
assert evm_result <= 5 %
assert abs(carrier_error) <= 20 kHz
```

`abs()` accepts `PowerDelta`, `FrequencyDelta`, `TemperatureDelta`, and `Ratio` and
returns the same type. It does not accept absolute quantities or Boolean values.
The operators `<`, `<=`, `>`, `>=`, `==`, and `!=` require operands of the same
non-Boolean type and produce `Boolean`. A frequency literal directly compared with
a known `FrequencyDelta` is contextually treated as a delta; named `Frequency`
values are never converted this way. An `assert` expression must be Boolean.
`abs` is reserved as the built-in function name and cannot be redeclared as a
measurement or derived value.

Assertions receive stable IDs `assertion-1`, `assertion-2`, and so on in source
order. Every assertion is evaluated once at every successfully completed point.
A false outcome does not stop later assertions or points. Each outcome retains its
source span, typed operands, comparison, coordinates, and pass/fail state. A point
passes only if all its assertions pass; the experiment passes only if every point
passes. Programs with no assertions have `not_evaluated` qualification status.

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
typed coordinate tuple and one strongly typed correction entry per sample, in the
same deterministic order as runtime points. The artifact also records ordered
dimension names and types. The legacy one-frequency vectors remain available to
existing C++ callers; there is no calibration application statement in the language.

## Structured JSON results

`horusrf-run experiment.hrf --json result.json` writes deterministic UTF-8 JSON
with schema name `horusrf.characterization-result` and major version `1`. It
contains qualification totals, every point's typed coordinates, all measurements
and derived values in declaration order, assertion outcomes and source spans,
calibration surface entries, and explicitly scoped metrics. Numeric objects carry
a stable type name, canonical value/unit, and display value/unit; coordinate
display units preserve the sweep declaration. Readers must ignore unknown object
members but reject an unknown schema name or unsupported major version.

The `metrics.calibration_fit` object contains the in-sample pre/post-correction
statistics. `metrics.qualification` contains assertion status and pass ratio.
`metrics.validation` is `null` because the language has no independent validation
data set. A zero corrected RMS is encoded with a null improvement ratio and reason
`perfect_fit`, never a non-finite JSON number. Programs without assertions use
`not_evaluated`, empty assertion arrays, and a null qualification pass ratio.

`--check`, `--csv`, and `--json` are mutually exclusive. JSON is written after a
completed execution even when qualification fails (exit 6). Parse, semantic,
lowering, runtime, or output failures leave no newly requested JSON artifact.

## Diagnostics

Source diagnostics use a stable lowercase code and the source span's beginning:

```text
experiment.hrf:2:11: parse.invalid_token: invalid token '@'
experiment.hrf:2:21: semantic.unexpected_quantity_type: expected Power, got PowerDelta
```

Parse, semantic, lowering, and runtime codes begin with `parse.`, `semantic.`,
`lowering.`, and `runtime.` respectively. A source diagnostic exits the CLI with
status 4, writes to standard error, emits no success output, and does not create a
requested CSV or JSON file. Usage errors exit 2 and file I/O errors exit 3.

A completed run with a false assertion prints its qualification summary, emits a
source-positioned `qualification.assertion_failed` diagnostic for each failed
outcome, and exits with status 6. This is not an execution failure. Legacy programs
have no qualification lines added to their console summary. The legacy CSV cannot
represent assertions and rejects such a request before execution.

`horusrf-run <source.hrf> --check` performs parsing, semantic analysis, and lowering
without constructing devices or executing the characterization. A successful check
is silent and exits with status 0. `--check`, `--csv`, and `--json` are mutually
exclusive.

Examples of rejected programs include a missing unit (`reference power = 10`), an
unknown name (`derive x = missing`), a non-positive step, and a calibration indexed
over `power` rather than `frequency`.

## Current limitations

The demonstration has no conditionals, general loops, user-defined functions, arrays, strings,
interpolation, multiple characterizations, phase/gain dimensions, instrument
declarations, procedural DSL statements,
calibration import or persistence, or explicit calibration application. These
omissions define the current scope.
