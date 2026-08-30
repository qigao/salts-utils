int runtime_path_dependency_value(void);
int runtime_path_extra_dependency_value(void);

int main(void) {
  return runtime_path_dependency_value() + runtime_path_extra_dependency_value() == 49 ? 0 : 1;
}
