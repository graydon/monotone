#if defined(__alpha) || defined(__sparcv9) || defined(__amd64)
#define SQLITE_PTR_SZ 8
#else
#define SQLITE_PTR_SZ 4
#endif

