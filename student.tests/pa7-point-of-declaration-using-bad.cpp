namespace Later {
int unavailable(int);
}
namespace ScopeOrder {
int before_using() { return unavailable(1); }
using namespace Later;
}
