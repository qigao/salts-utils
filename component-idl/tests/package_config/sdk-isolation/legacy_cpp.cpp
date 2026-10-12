#include <data_bind.h>
#include <cmeta/cmeta.h>
#include <type_traits>
static_assert(std::is_pointer_v<DataBind *>,
              "Installed native DataBind pointer contract must be usable in C++17");
int main() {
    DataBind *borrowed = nullptr;
    return borrowed == nullptr ? 0 : 1;
}
