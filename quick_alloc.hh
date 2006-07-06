#ifndef __QUICK_ALLOC__
#define __QUICK_ALLOC__

#if defined(__GNUC__) && __GNUC__ < 3 || (__GNUC__ == 3 && __GNUC_MINOR__ < 4)
# define QA(T) std::__allocator< T, std::__single_client_alloc >
# define QA_SUPPORTED
#else
# define QA(T) std::allocator< T >
#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif
