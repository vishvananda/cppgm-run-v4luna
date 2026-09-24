namespace ImportedType { typedef int Value; }
namespace Hiding {
using namespace ImportedType;
int Value;
int call() { return Value(1); }
}
