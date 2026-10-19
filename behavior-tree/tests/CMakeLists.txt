cmake_add_test(salts_behavior_tree_test
  SOURCES behavior_tree_test.c
  LIBS Salts::BehaviorTree Salts::TinyTest
  FOLDER "behavior-tree/tests")

set_target_properties(salts_behavior_tree_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
