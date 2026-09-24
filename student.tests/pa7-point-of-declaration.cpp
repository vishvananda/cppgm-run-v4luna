int choose(long);
int run() { return choose(1); }
int choose(int);

long outer = 7;
int local_scope() {
  long result = outer;
  int outer = 11;
  return result;
}

namespace Selected {
int imported(int);
}
namespace ScopeOrder {
using namespace Selected;
int after_using() { return imported(1); }
}

typedef long LocalType;
int local_type_before_hiding() {
  long before = LocalType(1);
  int LocalType = 2;
  return before + LocalType;
}
