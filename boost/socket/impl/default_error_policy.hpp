// Copyright (C) 2002 Hugo Duncan

// Permission to use, copy, modify, distribute and sell this software
// and its documentation for any purpose is hereby granted without fee,
// provided that the above copyright notice appear in all copies and
// that both that copyright notice and this permission notice appear
// in supporting documentation.  Hugo Duncan makes no representations
// about the suitability of this software for any purpose.
// It is provided "as is" without express or implied warranty.

#ifdef _MSC_VER
#pragma once
#endif

/// include guard
#ifndef DEFAULT_ERROR_POLICY_HPP
#define DEFAULT_ERROR_POLICY_HPP 1

#include "boost/socket/socket_errors.hpp"
#include "boost/socket/socket_exception.hpp"
#include "boost/throw_exception.hpp"

#include <iostream>

namespace boost
{
  namespace socket
  {

    struct default_error_policy
    {
      socket_errno handle_error(function::name fn, socket_errno error)
      {
        std::cerr << "Error in function " << fn << std::endl;

        switch (error)
        {
          case WouldBlock :
            std::cerr << "Would block " << fn << std::endl;
            return WouldBlock;
          case address_already_in_use :
            boost::throw_exception(
              socket_exception("Address already in use",error));;
          case address_family_not_supported_by_protocol_family :
            boost::throw_exception(
              socket_exception("Address family not supported by protocol family",error));;
          case address_not_available :
            boost::throw_exception(
              socket_exception("Address not available",error));;
          case bad_address :
            boost::throw_exception(
              socket_exception("Bad address",error));;
          case bad_protocol_option :
            boost::throw_exception(
              socket_exception("Bad protocol option",error));;
          case cannot_assign_requested_address :
            boost::throw_exception(
              socket_exception("Cannot assign requested address",error));;
          case cannot_send_after_socket_shutdown:
            boost::throw_exception(
              socket_exception("Can't send after socket shutdown",error));
          case connection_aborted :
            boost::throw_exception(
              socket_exception("Connection aborted",error));;
          case connection_already_in_progress :
            boost::throw_exception(
              socket_exception("Connection already in progress",error));;
          case connection_refused :
            boost::throw_exception(
              socket_exception("Connection refused",error));;
          case connection_reset_by_peer :
            boost::throw_exception(
              socket_exception("Connection reset by peer",error));;
          case connection_timed_out :
            boost::throw_exception(
              socket_exception("Connection timed out",error));;
          case destination_address_required :
            boost::throw_exception(
              socket_exception("Destination address required",error));;
          case graceful_shutdown_in_progress :
            boost::throw_exception(
              socket_exception("Graceful shutdown in progress",error));;
          case host_is_down :
            boost::throw_exception(
              socket_exception("Host is down",error));;
          case host_is_unreachable :
            boost::throw_exception(
              socket_exception("Host is unreachable",error));;
          case host_not_found :
            boost::throw_exception(
              socket_exception("Host not found",error));;
          case insufficient_memory_available :
            boost::throw_exception(
              socket_exception("Insufficient memory available",error));;
          case interrupted_function_call :
            boost::throw_exception(
              socket_exception("Interrupted function call",error));;
          case invalid_argument :
            boost::throw_exception(
              socket_exception("Invalid argument",error));;
          case message_too_long :
            boost::throw_exception(
              socket_exception("Message too long",error));;
          case net_reset :
            boost::throw_exception(
              socket_exception("net reset",error));;
          case network_dropped_connection_on_reset :
            boost::throw_exception(
              socket_exception("Network dropped connection on reset",error));;
          case network_interface_is_not_configured :
            boost::throw_exception(
              socket_exception("Network interface is not configured",error));;
          case network_is_down :
            boost::throw_exception(
              socket_exception("Network is down",error));;
          case network_is_unreachable :
            boost::throw_exception(
              socket_exception("Network is unreachable",error));;
          case network_subsystem_is_unavailable :
            boost::throw_exception(
              socket_exception("Network subsystem is unavailable",error));;
          case no_buffer_space_available :
            boost::throw_exception(
              socket_exception("No buffer space available",error));;
          case no_route_to_host :
            boost::throw_exception(
              socket_exception("No route to host",error));;
          case nonauthoritative_host_not_found :
            boost::throw_exception(
              socket_exception("Nonauthoritative host not found",error));;
          case not_a_valid_descriptor :
            boost::throw_exception(
              socket_exception("not a valid descriptor",error));;
          case one_or_more_parameters_are_invalid :
            boost::throw_exception(
              socket_exception("One or more parameters are invalid",error));;
          case operation_already_in_progress :
            boost::throw_exception(
              socket_exception("Operation already in progress",error));;
          case operation_not_supported :
            boost::throw_exception(
              socket_exception("Operation not supported",error));;
          case operation_not_supported_on_transport_endpoint :
            boost::throw_exception(
              socket_exception("Operation not supported on transport endpoint",error));;
          case operation_now_in_progress :
            boost::throw_exception(
              socket_exception("Operation now in progress",error));;
          case overlapped_operation_aborted :
            boost::throw_exception(
              socket_exception("Overlapped operation aborted",error));;
          case permission_denied:
            boost::throw_exception(
              socket_exception("Permission denied",error));;
          case protocol_family_not_supported :
            boost::throw_exception(
              socket_exception("Protocol family not supported",error));;
          case protocol_not_available :
            boost::throw_exception(
              socket_exception("Protocol not available",error));;
          case protocol_wrong_type_for_socket :
            boost::throw_exception(
              socket_exception("Protocol wrong type for socket",error));;
          case socket_is_already_connected :
            boost::throw_exception(
              socket_exception("Socket is already connected",error));;
          case socket_is_not_connected :
            boost::throw_exception(
              socket_exception("Socket is not connected",error));;
          case socket_operation_on_nonsocket :
            boost::throw_exception(
              socket_exception("Socket operation on nonsocket",error));;
          case socket_type_not_supported :
            boost::throw_exception(
              socket_exception("Socket type not supported",error));;
          case software_caused_connection_abort :
            boost::throw_exception(
              socket_exception("Software caused connection abort",error));;
          case specified_event_object_handle_is_invalid :
            boost::throw_exception(
              socket_exception("Specified event object handle is invalid",error));;
          case system_call_failure :
            boost::throw_exception(
              socket_exception("System call failure",error));;
          case this_is_a_nonrecoverable_error :
            boost::throw_exception(
              socket_exception("This is a nonrecoverable error",error));;
          case too_many_open_files :
            boost::throw_exception(
              socket_exception("Too many open files",error));;
          case too_many_processes :
            boost::throw_exception(
              socket_exception("Too many processes",error));;
          case unknown_protocol :
            boost::throw_exception(
              socket_exception("Unknown protocol",error));;
          case system_specific_error :
            boost::throw_exception(
              socket_exception("System specific error",error));;
          default:
            boost::throw_exception(
              socket_exception("Unknown error",error));;
        }
        // this is here as the compiler can not know if throw_exception
        // will throw !
        return system_specific_error;
      }

    };

  }// namespace
}// namespace

#endif
