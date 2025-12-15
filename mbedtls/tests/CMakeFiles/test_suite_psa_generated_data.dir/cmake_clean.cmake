file(REMOVE_RECURSE
  "CMakeFiles/test_suite_psa_generated_data"
  "suites/test_suite_psa_crypto_generate_key.generated.data"
  "suites/test_suite_psa_crypto_low_hash.generated.data"
  "suites/test_suite_psa_crypto_not_supported.generated.data"
  "suites/test_suite_psa_crypto_op_fail.generated.data"
  "suites/test_suite_psa_crypto_storage_format.current.data"
  "suites/test_suite_psa_crypto_storage_format.v0.data"
)

# Per-language clean rules from dependency scanning.
foreach(lang )
  include(CMakeFiles/test_suite_psa_generated_data.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
