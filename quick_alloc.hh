#ifndef __QUICK_ALLOC__
#define __QUICK_ALLOC__

#define QA(__klass) std::__allocator< __klass , std::__single_client_alloc >

#endif
