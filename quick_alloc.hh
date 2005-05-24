#ifndef __QUICK_ALLOC__
#define __QUICK_ALLOC__

#if defined(__GNUC__) && __GNUC__ < 3 || (__GNUC__ == 3 && __GNUC_MINOR__ < 4)
# define QA(T) std::__allocator< T, std::__single_client_alloc >
# define QA_SUPPORTED
#else 
# define QA(T) std::allocator< T >
#endif

#endif
