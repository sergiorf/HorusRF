# ProgramFacts and curated requirement metadata design

**Status:** M6Q.0 design note for M6Q.1

## Boundary

`ProgramFacts` is an AI/evaluation-owned, read-only projection of a successfully
lowered `ir::Program`. Construction must use `frontend::validate()` followed by
`ir::lower()`; it must not parse source independently or become a second type
system. Public and cross-layer quantities remain `Frequency`, `Power`, and
`PowerDelta`, with existing typed temperature and ratio quantities where
required.

The projection has no runtime or device dependency and cannot execute a script.
It reuses the expression normalization currently owned by strict semantic
comparison; that normalization should be extracted into a small shared internal
component rather than duplicated.

## Initial fact model

```text
ProgramFacts
  reference_power: Power
  sweeps: ordered SweepFact[]
  measurements: MeasurementFact[]
  derived_values: DerivedFact[]
  assertions: AssertionFact[]
  calibration: CalibrationFact

SweepFact
  dimension, canonical start/end/step, point_count, order

MeasurementFact
  operation, requested-visible name, value type, legacy-name flag

DerivedFact
  name, value type, normalized expression

AssertionFact
  ordinal, operator, operand type, normalized left/right expressions

CalibrationFact
  name, PowerDelta correction expression, ordered index dimensions
```

Names are retained where the user can explicitly require them. Canonical
quantities make equivalent unit spellings equal. Source spans may be retained
for explanations but are not part of requirement equality.

## Curated requirements

Golden metadata advances to its own versioned schema. Each supported case gains
an ordered `requirements` array. A requirement has a stable ID, kind, expected
typed fields, and provenance. Initial kinds are:

- `reference_power`;
- `sweep` (dimension, canonical range and optional step/order);
- `measurement` (operation and optional required name);
- `assertion` (normalized operands, operator, threshold);
- `calibration` (normalized correction and ordered indexes).

An omitted field is not a wildcard invented by the verifier; it means that the
curated requirement does not constrain that property. In particular, a
one-point frequency sweep whose user request omits a representational step must
omit `step` from its requirement.

Example shape:

```json
{
  "schema": "horusrf.golden-case",
  "version": 2,
  "requirements": [
    {
      "id": "frequency-range",
      "kind": "sweep",
      "dimension": "frequency",
      "start_hz": 2400000000,
      "end_hz": 2500000000,
      "step_hz": 10000000,
      "order": 0,
      "provenance": "curated"
    },
    {
      "id": "output-power",
      "kind": "measurement",
      "operation": "power",
      "required_name": "output_power",
      "provenance": "curated"
    }
  ]
}
```

The loader rejects unknown versions, duplicate requirement IDs, dimension/unit
mismatches, non-finite values, invalid ranges, unsupported operations, and
requirements inconsistent with `expected_valid` or comparison mode.

## Verification result

Each clause produces exactly one outcome: `satisfied`, `violated`, `unknown`, or
`unsupported`. It includes the requirement ID, a stable result code, expected
and actual typed renderings, and a concise explanation. Request-level status is
derived without erasing clause results.

Curated metadata is authoritative for golden evaluation. Later interactive
extraction supplies the same clause types with text provenance and confidence;
ambiguous extraction yields `unknown` rather than a guessed requirement.

Strict full-program comparison remains a secondary signal. Requirement success
does not imply strict equality, and strict mismatch does not imply requirement
failure.

## M6Q.1 implementation order

1. Extract reusable expression normalization from semantic comparison.
2. Implement `ProgramFacts` from valid `ir::Program` and unit-test the projection.
3. Add schema-v2 golden metadata parsing while retaining schema-v1 fixtures
   during migration.
4. Curate requirements for all 17 cases.
5. Implement per-clause verification and stable JSON report fields.
6. Reclassify algebraically equivalent calibration and unspecified one-point
   step cases, retaining strict semantic results alongside the new oracle.
