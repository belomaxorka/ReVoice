add_executable(test_voice_flood "test_voice_flood.cpp")

target_include_directories(test_voice_flood PRIVATE "${CMAKE_SOURCE_DIR}/revoice/src")

set_target_properties(test_voice_flood PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(NAME voice_flood COMMAND test_voice_flood)
