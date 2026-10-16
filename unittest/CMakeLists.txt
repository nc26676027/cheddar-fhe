include(FetchContent)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG release-1.12.1
)
FetchContent_MakeAvailable(googletest)

FetchContent_Declare(
  jsoncpp
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(jsoncpp)

add_compile_definitions(
  PARAM_DIR="${CMAKE_CURRENT_BINARY_DIR}"
)

add_executable(basic_test BasicTest.cpp)
target_link_libraries(basic_test
                      PRIVATE cheddar gtest_main nlohmann_json::nlohmann_json)

add_executable(user_interface_test UserInterfaceTest.cpp)
target_link_libraries(user_interface_test
                      PRIVATE cheddar gtest_main nlohmann_json::nlohmann_json)
add_executable(centered_crt_test CenteredCRTTest.cu)
target_link_libraries(centered_crt_test
                      PRIVATE cheddar gtest_main nlohmann_json::nlohmann_json)
add_executable(mod_up_to_max_test ModUpToMaxTest.cpp)
target_link_libraries(mod_up_to_max_test
                      PRIVATE cheddar gtest_main nlohmann_json::nlohmann_json)
add_executable(low_q0p_entry_test LowQ0PEntryTest.cpp)
target_link_libraries(low_q0p_entry_test
                      PRIVATE cheddar gtest_main nlohmann_json::nlohmann_json)

if (ENABLE_EXTENSION)
  find_package(ZLIB REQUIRED)
  add_executable(high_precision_encoding_test HighPrecisionEncodingTest.cpp)
  target_link_libraries(high_precision_encoding_test
                        PRIVATE cheddar gtest_main nlohmann_json::nlohmann_json)
  add_executable(meboot_test MEBootTest.cpp)
  add_executable(discrete_integer_cleaner_test DiscreteIntegerCleanerTest.cpp)
  target_link_libraries(discrete_integer_cleaner_test
                        PRIVATE cheddar gtest_main nlohmann_json::nlohmann_json)
  add_executable(discrete_identity_baseline_test DiscreteIdentityBaselineTest.cpp)
  target_link_libraries(discrete_identity_baseline_test
                        PRIVATE cheddar gtest_main nlohmann_json::nlohmann_json)
  add_executable(meboot_discrete_identity_test MEBootDiscreteIdentityTest.cpp)
  target_link_libraries(meboot_discrete_identity_test
                        PRIVATE cheddar gtest_main nlohmann_json::nlohmann_json)
  configure_file(${CMAKE_SOURCE_DIR}/parameters/research/discrete_direct5_d47.json
                 ${CMAKE_CURRENT_BINARY_DIR}/discrete_direct5_d47.json COPYONLY)
  configure_file(${CMAKE_SOURCE_DIR}/unittest/fixtures/discrete-direct5-known-answers.json
                 ${CMAKE_CURRENT_BINARY_DIR}/discrete-direct5-known-answers.json COPYONLY)
  configure_file(${CMAKE_SOURCE_DIR}/parameters/research/discrete_direct6_d95.json
                 ${CMAKE_CURRENT_BINARY_DIR}/discrete_direct6_d95.json COPYONLY)
  file(SHA256 ${CMAKE_SOURCE_DIR}/parameters/research/discrete_direct6_k21_remez_d63.json
       remez_manifest_sha256)
  file(READ ${CMAKE_SOURCE_DIR}/docs/evidence/discrete-identity/remez-20261016/direct6-certificate.json
       remez_certificate)
  string(JSON remez_certified_sha256 GET "${remez_certificate}" manifest_sha256)
  string(JSON remez_certificate_passed GET "${remez_certificate}" extraction_contract_passed)
  if(NOT remez_manifest_sha256 STREQUAL remez_certified_sha256 OR
     NOT remez_certificate_passed)
    message(FATAL_ERROR "Remez manifest must match its passing continuum certificate")
  endif()
  configure_file(${CMAKE_SOURCE_DIR}/parameters/research/discrete_direct6_k21_remez_d63.json
                 ${CMAKE_CURRENT_BINARY_DIR}/discrete_direct6_k21_remez_d63.json COPYONLY)
  configure_file(${CMAKE_SOURCE_DIR}/unittest/fixtures/discrete-direct6-known-answers.json
                 ${CMAKE_CURRENT_BINARY_DIR}/discrete-direct6-known-answers.json COPYONLY)
  configure_file(${CMAKE_SOURCE_DIR}/unittest/fixtures/discrete-direct6-remez-known-answers.json
                 ${CMAKE_CURRENT_BINARY_DIR}/discrete-direct6-remez-known-answers.json COPYONLY)
  target_link_libraries(meboot_test PRIVATE cheddar gtest_main nlohmann_json::nlohmann_json ZLIB::ZLIB)
  add_executable(meboot_performance_test MEBootPerformanceTest.cpp)
  target_link_libraries(meboot_performance_test PRIVATE cheddar gtest_main nlohmann_json::nlohmann_json)

  add_executable(meboot_matrix_planner_test MEBootMatrixPlannerTest.cpp)
  target_link_libraries(meboot_matrix_planner_test
                        PRIVATE cheddar gtest_main nlohmann_json::nlohmann_json ZLIB::ZLIB)

  add_executable(aks_key_test AKSKeyTest.cpp)
  target_link_libraries(aks_key_test
                        PRIVATE cheddar gtest_main nlohmann_json::nlohmann_json)

  add_executable(lcr_step_test LCRStepTest.cpp)
  target_link_libraries(lcr_step_test
                        PRIVATE cheddar gtest_main nlohmann_json::nlohmann_json)

  add_executable(meboot_cts_test MEBootCtSTest.cpp)
  target_link_libraries(meboot_cts_test
                        PRIVATE cheddar gtest_main nlohmann_json::nlohmann_json)

  add_executable(linear_transform_test LinearTransformTest.cpp)
  target_link_libraries(linear_transform_test
                        PRIVATE cheddar gtest_main nlohmann_json::nlohmann_json)

endif()

configure_file(${CMAKE_SOURCE_DIR}/parameters/bootparam_30.json
               ${CMAKE_CURRENT_BINARY_DIR}/bootparam_30.json COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/parameters/bootparam_35.json
               ${CMAKE_CURRENT_BINARY_DIR}/bootparam_35.json COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/parameters/bootparam_40.json
               ${CMAKE_CURRENT_BINARY_DIR}/bootparam_40.json COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/parameters/bootparam_40_64bit.json
               ${CMAKE_CURRENT_BINARY_DIR}/bootparam_40_64bit.json COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/parameters/bootparam_40_64bit_lowp_entry.json
               ${CMAKE_CURRENT_BINARY_DIR}/bootparam_40_64bit_lowp_entry.json COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/parameters/identity19_meboot.json
               ${CMAKE_CURRENT_BINARY_DIR}/identity19_meboot.json COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/parameters/ordinary.json
               ${CMAKE_CURRENT_BINARY_DIR}/ordinary.json COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/unittest/fixtures/identity19-stage-oracle.json.gz
               ${CMAKE_CURRENT_BINARY_DIR}/identity19-stage-oracle.json.gz COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/parameters/meboot_golden_4434.json
               ${CMAKE_CURRENT_BINARY_DIR}/meboot_golden_4434.json COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/parameters/meboot_golden_4434.bin.gz
               ${CMAKE_CURRENT_BINARY_DIR}/meboot_golden_4434.bin.gz COPYONLY)

add_test(NAME basic_test COMMAND basic_test)
add_test(NAME user_interface_test COMMAND user_interface_test)
add_test(NAME centered_crt_test COMMAND centered_crt_test)
add_test(NAME mod_up_to_max_test COMMAND mod_up_to_max_test)
add_test(NAME low_q0p_entry_test COMMAND low_q0p_entry_test)
set_tests_properties(low_q0p_entry_test PROPERTIES RUN_SERIAL TRUE)
set_tests_properties(user_interface_test centered_crt_test mod_up_to_max_test PROPERTIES RUN_SERIAL TRUE)
if (ENABLE_EXTENSION)
  add_test(NAME high_precision_encoding_test COMMAND high_precision_encoding_test)
  set_tests_properties(high_precision_encoding_test PROPERTIES RUN_SERIAL TRUE)
  add_test(NAME discrete_integer_cleaner_test COMMAND discrete_integer_cleaner_test)
  set_tests_properties(discrete_integer_cleaner_test PROPERTIES RUN_SERIAL TRUE)
  add_test(NAME discrete_identity_baseline_test COMMAND discrete_identity_baseline_test)
  set_tests_properties(discrete_identity_baseline_test PROPERTIES RUN_SERIAL TRUE)
  add_test(NAME meboot_discrete_identity_test COMMAND meboot_discrete_identity_test)
  set_tests_properties(meboot_discrete_identity_test PROPERTIES RUN_SERIAL TRUE)
  add_test(NAME meboot_test COMMAND meboot_test)
  set_tests_properties(meboot_test PROPERTIES RUN_SERIAL TRUE)
  add_test(NAME meboot_performance_test COMMAND meboot_performance_test)
  set_tests_properties(meboot_performance_test PROPERTIES RUN_SERIAL TRUE)
  foreach(test_target meboot_matrix_planner_test aks_key_test
                      lcr_step_test meboot_cts_test linear_transform_test)
    add_test(NAME ${test_target} COMMAND ${test_target})
    set_tests_properties(${test_target} PROPERTIES RUN_SERIAL TRUE)
  endforeach()
endif()
