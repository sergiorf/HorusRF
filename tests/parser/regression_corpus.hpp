#pragma once

#include <array>
#include <string_view>

namespace horusrf::test::parser_regression {

enum class ExampleClassification { ValidProgram, SemanticError };

struct ExampleCase {
    std::string_view path;
    ExampleClassification classification;
};

inline constexpr std::array example_cases{
    ExampleCase{"examples/tx_path_characterization.hrf", ExampleClassification::ValidProgram},
    ExampleCase{"examples/multidimensional_check.hrf", ExampleClassification::ValidProgram},
    ExampleCase{"examples/named_measurements.hrf", ExampleClassification::ValidProgram},
    ExampleCase{"examples/qualified_measurements.hrf", ExampleClassification::ValidProgram},
    ExampleCase{"examples/studio_default.hrf", ExampleClassification::ValidProgram},
    ExampleCase{"examples/studio_qualification.hrf", ExampleClassification::ValidProgram},
    ExampleCase{"examples/invalid/calibration_over_power.hrf", ExampleClassification::SemanticError},
    ExampleCase{"examples/invalid/missing_calibration.hrf", ExampleClassification::SemanticError},
    ExampleCase{"examples/invalid/power_plus_power.hrf", ExampleClassification::SemanticError},
    ExampleCase{"examples/invalid/reference_uses_db.hrf", ExampleClassification::SemanticError},
    ExampleCase{"examples/invalid/reversed_sweep.hrf", ExampleClassification::SemanticError},
    ExampleCase{"examples/invalid/use_before_measurement.hrf", ExampleClassification::SemanticError},
};

inline constexpr std::array valid_sources{
    std::string_view{"characterize empty {}"},
    std::string_view{"characterize c { measure result = vendor_operation }"},
    std::string_view{"characterize c { measure power = evm }"},
    std::string_view{"characterize c { derive value = power + frequency - temperature }"},
    std::string_view{"characterize c { derive value = power(frequency) }"},
    std::string_view{"characterize c { derive calibration cal { correction = reference.power "
                     "over frequency, temperature, power } }"},
    std::string_view{"characterize c { sweep frequency -1 Hz .. 2 kHz step 3 MHz "
                     "sweep temperature -4 K .. 5 degC step 6 deltaK "
                     "derive a = 7 GHz derive b = 8 dBm derive c = 9 dB "
                     "derive d = 10 deltaC derive e = 11 % }"},
    std::string_view{"// leading comment\r\ncharacterize c{// inline\n"
                     "assert (1 dB + -2 dB) != 3 dB}"},
};

inline constexpr std::array invalid_sources{
    std::string_view{""},
    std::string_view{"characterize power {}"},
    std::string_view{"characterize c {} characterize d {}"},
    std::string_view{"characterize c { reference power = 1 }"},
    std::string_view{"characterize c { derive x = .5 dB }"},
    std::string_view{"characterize c { derive x = 1. dB }"},
    std::string_view{"characterize c { derive x = 1e3 Hz }"},
    std::string_view{"characterize c { derive x = +1 dB }"},
    std::string_view{"characterize c { derive x = --a }"},
    std::string_view{"characterize c { derive x = a * b }"},
    std::string_view{"characterize c { assert a < b < c }"},
    std::string_view{"characterize c { derive x = f() }"},
    std::string_view{"characterize c { derive x = f(a, b) }"},
    std::string_view{"characterize c { sweep voltage 0 K .. 1 K step 1 K }"},
    std::string_view{"characterize c { measure result }"},
    std::string_view{"characterize c { derive frequency = 1 Hz }"},
    std::string_view{"characterize c { derive calibration c { correction = 1 dB over } }"},
    std::string_view{"characterize c { derive x = reference.measure }"},
    std::string_view{"characterize c { derive x = 1Hz }"},
    std::string_view{"characterize c { derive x = 1 dB; }"},
};

} // namespace horusrf::test::parser_regression
