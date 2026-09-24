template<class T> T identity(T value);
template<class T> void select(T value);
void select(int value);

int use_identity() { return identity(4); }
void use_nontemplate() { select(3); }
