cmake_minimum_required(VERSION 3.20)

foreach(required_variable IN ITEMS TP_EVIDENCE_DIR TP_CONFIG)
  if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
    message(FATAL_ERROR "Missing required variable: ${required_variable}")
  endif()
endforeach()

set(raw_interface_file "${TP_EVIDENCE_DIR}/raw-interface.txt")
set(evaluated_interface_file
  "${TP_EVIDENCE_DIR}/evaluated-interface-${TP_CONFIG}.txt")
foreach(evidence_file IN ITEMS
    "${raw_interface_file}" "${evaluated_interface_file}")
  if(NOT EXISTS "${evidence_file}")
    message(FATAL_ERROR
      "Installed TbeCBind interface evidence is missing: ${evidence_file}")
  endif()
endforeach()

file(READ "${raw_interface_file}" raw_interface)
file(READ "${evaluated_interface_file}" evaluated_interface)
string(STRIP "${raw_interface}" raw_interface)
string(STRIP "${evaluated_interface}" evaluated_interface)
set(expected_raw_interface
  "$<LINK_ONLY:TurboParser::TbeSchema>;TurboUtils::CBind")
set(expected_evaluated_interface
  "TurboParser::TbeSchema;TurboUtils::CBind")
if(NOT raw_interface STREQUAL expected_raw_interface OR
   NOT evaluated_interface STREQUAL expected_evaluated_interface)
  message(FATAL_ERROR
    "Installed TbeCBind interface contract mismatch:\n"
    "  expected raw: ${expected_raw_interface}\n"
    "  actual raw: ${raw_interface}\n"
    "  expected evaluated: ${expected_evaluated_interface}\n"
    "  actual evaluated: ${evaluated_interface}")
endif()

if(DEFINED TP_OUTPUT_FILE AND NOT "${TP_OUTPUT_FILE}" STREQUAL "")
  file(WRITE "${TP_OUTPUT_FILE}"
    "raw=${raw_interface}\nevaluated=${evaluated_interface}\n")
endif()
message(STATUS
  "Installed TbeCBind interface contract: ${evaluated_interface}")
