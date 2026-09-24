#define f(x) 1 g(x)
#define g(x) 2 f(x)
f(f(x))

#define z z[0]
#define f2(x) 1 x
f2(z)

#define f3(x) 1 x
#define g3(x) 2 x
g3(f3)(g3)(3)

#if 1 and not 0 and (3 bitand 1)
17
#endif

#if 0 or (2 xor 3)
19
#endif

#define NEXT(x) x
NEXT
(
23
)

_Pragma
(
"cppgm_mock_unknown"
)
29
