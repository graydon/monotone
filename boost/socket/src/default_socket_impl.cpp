// Copyright (C) 2002 Hugo Duncan

// Permission to use, copy, modify, distribute and sell this software
// and its documentation for any purpose is hereby granted without fee,
// provided that the above copyright notice appear in all copies and
// that both that copyright notice and this permission notice appear
// in supporting documentation.  Hugo Duncan makes no representations
// about the suitability of this software for any purpose.
// It is provided "as is" without express or implied warranty.

#ifdef _MSC_VER
#pragma warning (disable: 4786 4305)
  // 4786 truncated debug symbolic name
  // 4305 truncation from const double to float
#endif

#if defined(__BORLANDC__)
#pragma hdrstop
#endif

#include "boost/socket/impl/default_socket_impl.hpp"
#include "boost/socket/socket_exception.hpp"
#include "boost/socket/socket_errors.hpp"

#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
  #include <Winsock2.h>
#elif defined(__CYGWIN__)
  #include <sys/socket.h>
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <sys/ioctl.h>
  #include <sys/errno.h>
#else
  #include <sys/socket.h>
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <sys/ioctl.h>
  #include <sys/errno.h>
#endif

#ifdef _MSC_VER
#pragma warning (push, 4)
#pragma warning (disable: 4786 4305)
#endif

namespace boost
{
  namespace socket
  {
    namespace impl
    {

      default_socket_impl::default_socket_impl()
          : m_socket(invalid_socket)
      {}

      default_socket_impl::default_socket_impl(socket_t socket)
          : m_socket(socket)
      {}

      default_socket_impl::~default_socket_impl()
      {
        if (m_socket!=invalid_socket)
          close();
      }

      //! release the socket handle
      default_socket_impl::socket_t default_socket_impl::release()
      {
        socket_t socket=m_socket;
        m_socket=invalid_socket;
        return socket;
      }

      //! reset the socket handle
      void default_socket_impl::reset(socket_t socket)
      {
        if (m_socket!=invalid_socket)
          close();
        m_socket=socket;
      }

      socket_errno default_socket_impl::ioctl(int option, void* data)
      {
#ifdef USES_WINSOCK2
        return translate_error(
          ::ioctlsocket(m_socket, option, (unsigned long*)data) );
#else
        return translate_error( ::ioctl(m_socket, option, data) );
#endif
      }

      socket_errno default_socket_impl::getsockopt(
        int level, int optname, void *data, size_t& optlen)
      {
#if defined(USES_WINSOCK2) || defined(__CYGWIN__)
        int len=optlen;
        int ret = ::getsockopt(m_socket, level, optname, (char*)data, &len);
        optlen=len;
        return translate_error(ret);
#else
        return translate_error(
          ::getsockopt(m_socket, level, optname, data, &optlen) );
#endif
      }

      socket_errno default_socket_impl::setsockopt(
        int level,int optname, void const* data, size_t optlen)
      {
#ifdef USES_WINSOCK2
        return translate_error(
          ::setsockopt(m_socket, level, optname,
                       static_cast<char const*>(data),
                       optlen) );
#else
        return translate_error(
          ::setsockopt(m_socket, level, optname, data, optlen) );
#endif
      }

      socket_errno default_socket_impl::open(
        family_t family, protocol_type_t type, protocol_t protocol)
      {
        m_socket = ::socket(family, type, protocol);
        if (m_socket!=invalid_socket)
          return Success;
        return translate_error(-1);
      }

      socket_errno default_socket_impl::connect(
        const std::pair<void const*,size_t>& address)
      {
        return translate_error(
          ::connect(m_socket,
                    static_cast<sockaddr const*>(address.first),
                    address.second)
          );
      }

      socket_errno default_socket_impl::bind(
        const std::pair<void const*,size_t>& address)
      {
        return translate_error(
          ::bind(m_socket,
                 static_cast<sockaddr const*>(address.first),
                 address.second)
          );
      }

      socket_errno default_socket_impl::listen(int backlog)
      {
        return translate_error( ::listen(m_socket, backlog) );
      }

      //! accept a connection
      socket_errno default_socket_impl::accept(
        default_socket_impl& new_socket,
        std::pair<void *,size_t>& address)
      {
#if defined(USES_WINSOCK2) || defined(__CYGWIN__)
        int len=address.second;
        new_socket.reset(::accept(m_socket,
                                  static_cast<sockaddr*>(address.first),
                                  &len));
        address.second=len;
#else
        new_socket.reset(::accept(m_socket,
                                  static_cast<sockaddr*>(address.first),
                                  &address.second));
#endif
        if (new_socket.m_socket!=invalid_socket)
          return Success;
        return translate_error(-1);
      }

      //! receive data
      int default_socket_impl::recv(void* data, size_t len, int flags)
      {
        int ret=::recv(m_socket, (char*)data, len,flags);
        if (ret==-1)
          return translate_error(ret);
        if (ret==0)
          return close();
        return ret;
      }

      //! send data
      /** Returns the number of bytes sent */
      int default_socket_impl::send(const void* data, size_t len, int flags)
      {
        int ret = ::send(m_socket, (const char*)data, len, flags);
        if (ret==-1)
          return translate_error(ret);
        return ret;
      }

      //! shut the socket down
      socket_errno default_socket_impl::shutdown(Direction how)
      {
        return translate_error(::shutdown(m_socket, static_cast<int>(how)));
      }

      //! close the socket
      socket_errno default_socket_impl::close()
      {
#if defined(USES_WINSOCK2)
        int ret = ::closesocket(m_socket);
#else
        int ret = ::close(m_socket);
#endif
        m_socket=invalid_socket;
        return translate_error(ret);
      }

      //! check for a valid socket
      bool default_socket_impl::is_open() const
      {
        return m_socket!=invalid_socket;
      }

      //! obtain OS socket
      default_socket_impl::socket_t
      default_socket_impl::socket()
      {
        return m_socket;
      }

      //! obtain OS socket
      const default_socket_impl::socket_t
      default_socket_impl::socket() const
      {
        return m_socket;
      }

      //! compare a socket
      bool default_socket_impl::operator<(
        const default_socket_impl& socket) const
      {
        return m_socket<socket.m_socket;
      }

      //! compare a socket
      bool default_socket_impl::operator==(
        const default_socket_impl& socket) const
      {
        return m_socket==socket.m_socket;
      }

      //! compare a socket
      bool default_socket_impl::operator!=(
        const default_socket_impl& socket) const
      {
        return m_socket!=socket.m_socket;
      }



#if defined(USES_WINSOCK2)
      socket_errno default_socket_impl::translate_error(int return_value)
      {
        if (return_value==0)
          return Success;

        int error=WSAGetLastError();

        switch (error)
        {
          case 0:
            return Success;
          case WSAEWOULDBLOCK :
            return WouldBlock;
          case WSAEACCES :
            return boost::socket::permission_denied;
          case WSAEADDRINUSE :
            return boost::socket::address_already_in_use;
          case WSAEADDRNOTAVAIL :
            return boost::socket::cannot_assign_requested_address;
          case WSAEAFNOSUPPORT :
            return boost::socket::address_family_not_supported_by_protocol_family;
          case WSAEALREADY :
            return boost::socket::operation_already_in_progress;
          case WSAECONNABORTED :
            return boost::socket::software_caused_connection_abort;
          case WSAECONNREFUSED :
            return boost::socket::connection_refused;
          case WSAECONNRESET :
            return boost::socket::connection_reset_by_peer;
          case WSAEDESTADDRREQ :
            return boost::socket::destination_address_required;
          case WSAEFAULT :
            return boost::socket::bad_address;
          case WSAEHOSTDOWN :
            return boost::socket::host_is_down;
          case WSAEHOSTUNREACH :
            return boost::socket::no_route_to_host;
          case WSAEINPROGRESS :
            return boost::socket::operation_now_in_progress;
          case WSAEINTR :
            return boost::socket::interrupted_function_call;
          case WSAEINVAL :
            return boost::socket::invalid_argument;
          case WSAEMFILE :
            return boost::socket::too_many_open_files;
          case WSAEMSGSIZE :
            return boost::socket::message_too_long;
          case WSAENETDOWN :
            return boost::socket::network_is_down;
          case WSAENETRESET :
            return boost::socket::network_dropped_connection_on_reset;
          case WSAENETUNREACH :
            return boost::socket::network_is_unreachable;
          case WSAENOBUFS :
            return boost::socket::no_buffer_space_available;
          case WSAENOPROTOOPT :
            return boost::socket::bad_protocol_option;
          case WSAENOTCONN :
            return boost::socket::socket_is_not_connected;
          case WSAENOTSOCK :
            return boost::socket::socket_operation_on_nonsocket;
          case WSAEOPNOTSUPP :
            return boost::socket::operation_not_supported;
          case WSAEPFNOSUPPORT :
            return boost::socket::protocol_family_not_supported;
          case WSAEPROCLIM :
            return boost::socket::too_many_processes;
          case WSAEPROTONOSUPPORT :
            return boost::socket::protocol_not_available;
          case WSAEPROTOTYPE :
            return boost::socket::protocol_wrong_type_for_socket;
          case WSAESHUTDOWN :
            return boost::socket::cannot_send_after_socket_shutdown;
          case WSAESOCKTNOSUPPORT :
            return boost::socket::socket_type_not_supported;
          case WSAETIMEDOUT :
            return boost::socket::connection_timed_out;
          case WSATYPE_NOT_FOUND :
            return boost::socket::class_type_not_found;
          case WSAHOST_NOT_FOUND :
            return boost::socket::host_not_found;
          case WSA_INVALID_HANDLE :
            return boost::socket::specified_event_object_handle_is_invalid;
          case WSA_NOT_ENOUGH_MEMORY :
            return boost::socket::insufficient_memory_available;
          case WSANO_DATA :
            return boost::socket::valid_name_no_data_record_of_requested_type;
          case WSANO_RECOVERY :
            return boost::socket::this_is_a_nonrecoverable_error;
          case WSASYSCALLFAILURE :
            return boost::socket::system_call_failure;
          case WSASYSNOTREADY :
            return boost::socket::network_subsystem_is_unavailable;
          case WSATRY_AGAIN :
            return boost::socket::nonauthoritative_host_not_found;
          case WSAEDISCON :
            return boost::socket::graceful_shutdown_in_progress;
          case WSA_OPERATION_ABORTED :
            return boost::socket::overlapped_operation_aborted;

          case WSANOTINITIALISED :
          case WSAVERNOTSUPPORTED :
          case WSA_INVALID_PARAMETER :
          case WSA_IO_INCOMPLETE :
          case WSA_IO_PENDING :
          default:
            return boost::socket::system_specific_error;
        }
      }

#else

      socket_errno default_socket_impl::translate_error(int return_value)
      {
        if (return_value==0)
          return Success;

        switch (errno)
        {
          case 0:
            return Success;
          case EAGAIN :
            return WouldBlock;
          case EBADF :
            return boost::socket::not_a_valid_descriptor;
          case EOPNOTSUPP :
            return boost::socket::operation_not_supported_on_transport_endpoint;
          case EPFNOSUPPORT :
            return boost::socket::protocol_family_not_supported;
          case ECONNRESET :
            return boost::socket::connection_reset_by_peer;
          case ENOBUFS :
            return boost::socket::no_buffer_space_available;
          case EAFNOSUPPORT :
            return boost::socket::address_family_not_supported_by_protocol_family;
          case EPROTOTYPE :
            return boost::socket::protocol_wrong_type_for_socket;
          case ENOTSOCK :
            return boost::socket::socket_operation_on_nonsocket;
          case ENOPROTOOPT :
            return boost::socket::protocol_not_available;
          case ESHUTDOWN :
            return boost::socket::cannot_send_after_socket_shutdown;
          case ECONNREFUSED :
            return boost::socket::connection_refused;
          case EADDRINUSE :
            return boost::socket::address_already_in_use;
          case ECONNABORTED :
            return boost::socket::connection_aborted;
          case ENETUNREACH :
            return boost::socket::network_is_unreachable;
          case ENETDOWN :
            return boost::socket::network_interface_is_not_configured;
          case ETIMEDOUT :
            return boost::socket::connection_timed_out;
          case EHOSTDOWN :
            return boost::socket::host_is_down;
          case EHOSTUNREACH :
            return boost::socket::host_is_unreachable;
          case EINPROGRESS :
            return boost::socket::connection_already_in_progress;
          case EALREADY :
            return boost::socket::socket_is_already_connected;
          case EDESTADDRREQ :
            return boost::socket::destination_address_required;
          case EMSGSIZE :
            return boost::socket::message_too_long;
          case EPROTONOSUPPORT :
            return boost::socket::unknown_protocol;
          case ESOCKTNOSUPPORT :
            return boost::socket::socket_type_not_supported;
          case EADDRNOTAVAIL :
            return boost::socket::address_not_available;
          case ENETRESET :
            return boost::socket::net_reset;
          case EISCONN :
            return boost::socket::socket_is_already_connected;
          case ENOTCONN :
            return boost::socket::socket_is_not_connected;
          default:
            return boost::socket::system_specific_error;
        }
      }
#endif

    }// namespace
  }// namespace
}// namespace

#ifdef _MSC_VER
#pragma warning (pop)
#endif
