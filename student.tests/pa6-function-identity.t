void equivalent(int value);
void equivalent(const int value);

void adjusted(int values[]);
void adjusted(int* values);

struct MemberQualifiers {
  void cv() const;
  void cv() volatile;
  void ref() &;
  void ref() &&;
};

struct Operators {
  int operator+(int value) const;
  int operator+(const int value) const;
  int operator-(int value) const;
};

template<class T> struct FirstTemplate {
  T value;
};

template<class T> struct SecondTemplate {
  T value;
};
