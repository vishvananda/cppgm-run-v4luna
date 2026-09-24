enum class Choice : int;
int before_definition() { return static_cast<int>(Choice::ready); }
enum class Choice : int { ready };
