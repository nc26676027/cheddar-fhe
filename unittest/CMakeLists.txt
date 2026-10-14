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
  add_executable(identity16_test Identity16Test.cpp)
  target_link_libraries(identity16_test PRIVATE cheddar gtest_main nlohmann_json::nlohmann_json)
  add_executable(identity16_performance_test Identity16PerformanceTest.cpp)
  target_link_libraries(identity16_performance_test PRIVATE cheddar gtest_main nlohmann_json::nlohmann_json)
  add_executable(boot_test Bootstrapping.cpp)
  add_executable(eval_mod_range_test EvalModRangeTest.cpp)
  target_link_libraries(eval_mod_range_test PRIVATE cheddar gtest_main nlohmann_json::nlohmann_json)
  target_link_libraries(boot_test
                        PRIVATE cheddar gtest_main nlohmann_json::nlohmann_json)

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

  add_executable(meboot_performance_test MEBootPerformanceTest.cpp)
  target_link_libraries(meboot_performance_test
                        PRIVATE cheddar gtest_main nlohmann_json::nlohmann_json)
  add_executable(stc_tuning_test StCTuningTest.cpp)
  target_link_libraries(stc_tuning_test
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
configure_file(${CMAKE_SOURCE_DIR}/parameters/identity16_joint4434_selected.json
               ${CMAKE_CURRENT_BINARY_DIR}/identity16_joint4434_selected.json COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/parameters/identity16_ordinary_selected.json
               ${CMAKE_CURRENT_BINARY_DIR}/identity16_ordinary_selected.json COPYONLY)
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
  add_test(NAME identity16_test COMMAND identity16_test)
  set_tests_properties(identity16_test PROPERTIES RUN_SERIAL TRUE)
  add_test(NAME identity16_performance_test COMMAND identity16_performance_test)
  set_tests_properties(identity16_performance_test PROPERTIES RUN_SERIAL TRUE)
  foreach(test_target boot_test eval_mod_range_test meboot_matrix_planner_test aks_key_test
                      lcr_step_test meboot_cts_test linear_transform_test)
    add_test(NAME ${test_target} COMMAND ${test_target})
    set_tests_properties(${test_target} PROPERTIES RUN_SERIAL TRUE)
  endforeach()
  add_test(NAME meboot_performance_test COMMAND meboot_performance_test)
  set_tests_properties(meboot_performance_test PROPERTIES RUN_SERIAL TRUE)
  add_test(NAME stc_tuning_test COMMAND stc_tuning_test)
  set_tests_properties(stc_tuning_test PROPERTIES RUN_SERIAL TRUE)
endif()
