void inspect_local_types() {
  { struct Local { int first; }; }
  { struct Local { char second; }; }
}

template<class T> struct FirstTemplate { T value; };
template<class T> struct SecondTemplate { T value; };

void cv_parameter(const int);
void cv_parameter(int);
void array_parameter(int values[4]);
void array_parameter(int* values);
void callback_parameter(void callback(int));
void callback_parameter(void (*callback)(int));

extern int completed[];
int completed[4];
