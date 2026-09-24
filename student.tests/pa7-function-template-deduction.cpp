template<class T> T identity(T value);
template<class T> void select(T value);
template<class T> void mark(int value);
void select(int value);

int use_identity() { return identity(4); }
void use_nontemplate() { select(3); }
void use_distinct_template_arguments() { mark<int>(1); mark<long>(2); }
