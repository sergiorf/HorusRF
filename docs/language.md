# HorusRF M0 language reference

This document describes the implemented M0 language. The authoritative concrete
syntax is [`grammar/horusrf.ebnf`](../grammar/horusrf.ebnf); syntax changes must keep
the grammar, lexer, parser, this reference, and tests synchronized.

## Lexical rules and source positions

Whitespace separates tokens and `//` starts a comment through the end of its line.
Identifiers start with an ASCII letter and continue with ASCII letters, digits, or
underscores. Keywords are lowercase and unit spelling is exact. Decimal numbers
require a leading digit and, when a decimal point is present, digits after it;
scientific notation and leading-dot forms are rejected.

Punctuation is `{ } = .. . + - ( ) ,`. Units are `Hz`, `kHz`, `MHz`, `GHz`, `dBm`,
and `dB`. Source locations are one-based line and column numbers; spans use byte
offsets internally.

## Program and declarations

A source file contains exactly one named `characterize` block. The canonical
program is [`examples/tx_path_characterization.hrf`](../examples/tx_path_characterization.hrf):

```horusrf
characterize tx_path {
  reference power = -10 dBm
  sweep frequency 2.40 GHz .. 2.50 GHz step 1 MHz
  measure power
  derive error = power - reference.power
  derive calibration tx_power {
    correction = reference.power - power
    over frequency
  }
}
```

`reference power` declares the sole nominal comparison power. Its expression name
is `reference.power`. M0 requires exactly one reference, frequency sweep, and power
measurement. Duplicate required declarations are errors; a missing one is reported
after statement analysis.

The sweep endpoints and step must be frequency quantities. The step is strictly
positive and the end must not precede the start. Runtime generates points in
ascending order by integer index, includes a reachable endpoint, and rejects plans
above 1,000,000 points. `measure power` defines `power`; at runtime that sole
measurement is associated with each point of the sole active frequency sweep.

Statements are analyzed in written order. `frequency`, `power`, `reference.power`,
and derived names are usable only after their declarations. Forward references,
unknown names, duplicate user names, and attempts to reuse `frequency` or `power`
are rejected.

## Expressions and quantities

Expressions contain quantities, identifiers, `reference.power`, parentheses, unary
minus, and binary `+` and `-`. Binary operators have equal precedence and associate
left to right. Unary minus binds more tightly. Quantities normalize as follows:

| Source unit | Semantic type | Canonical storage |
|---|---|---|
| `Hz`, `kHz`, `MHz`, `GHz` | `Frequency` | Hz |
| `dBm` | `Power` | dBm |
| `dB` | `PowerDelta` | dB |

Only these arithmetic operations are supported:

| Expression | Result |
|---|---|
| `Power - Power` | `PowerDelta` |
| `Power + PowerDelta` | `Power` |
| `PowerDelta + PowerDelta` | `PowerDelta` |
| unary `-PowerDelta` | `PowerDelta` |

M0 rejects `Power + Power`, `PowerDelta + Power`, `PowerDelta - PowerDelta`,
`PowerDelta - Power`, `Power - PowerDelta`, unary `-Power`, and arithmetic involving
`Frequency`. Operand order is significant.

## Calibration artifacts

```horusrf
derive calibration tx_power {
  correction = reference.power - power
  over frequency
}
```

The correction must have `PowerDelta` type. `over` is mandatory and names explicit
index dimensions. Every index must already name an active sweep, and unknown,
non-sweep, or duplicate indexes are rejected. M0 has one frequency sweep, so the
canonical result is `tx_power : Frequency -> PowerDelta`. Runtime stores one
dimension and correction entry per sample and internally applies the correction
only to calculate verification metrics; there is no application statement.

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

Examples of rejected programs include a missing unit (`reference power = 10`), an
unknown name (`derive x = missing`), a non-positive step, and a calibration indexed
over `power` rather than `frequency`.

## Current limitations

M0 has no conditionals, general loops, functions, arrays, strings, interpolation,
multiple characterizations, multiple sweeps, phase/gain/temperature dimensions,
instrument declarations, procedural DSL statements, calibration import or
persistence, or explicit calibration application. Those are possible future
features and are not accepted syntax today.
