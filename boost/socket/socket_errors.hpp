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
#ifndef BOOST_SOCKET_SOCKET_ERRORS_HPP
#define BOOST_SOCKET_SOCKET_ERRORS_HPP 1

#include "boost/socket/config.hpp"
#include "boost/socket/socket_exception.hpp"
#include "boost/static_assert.hpp"

namespace boost
{
  namespace socket
  {

    //! function values passed to error policy
    namespace function
    {
      enum name {
        ioctl,
        getsockopt,
        setsockopt,
        open,
        connect,
        bind,
        listen,
        accept,
        recv,
        send,
        shutdown,
        close
      };
    }

    enum socket_errno {
      address_already_in_use=-127,
      address_family_not_supported_by_protocol_family,
      address_not_available,
      bad_address,
      bad_protocol_option,
      cannot_assign_requested_address,
      cannot_send_after_socket_shutdown,
      class_type_not_found,
      connection_aborted,
      connection_already_in_progress,
      connection_refused,
      connection_reset_by_peer,
      connection_timed_out,
      destination_address_required,
      graceful_shutdown_in_progress,
      host_is_down,
      host_is_unreachable,
      host_not_found,
      insufficient_memory_available,
      interrupted_function_call,
      invalid_argument,
      message_too_long,
      net_reset,
      network_dropped_connection_on_reset,
      network_interface_is_not_configured,
      network_is_down,
      network_is_unreachable,
      network_subsystem_is_unavailable,
      no_buffer_space_available,
      no_route_to_host,
      nonauthoritative_host_not_found,
      not_a_valid_descriptor,
      one_or_more_parameters_are_invalid,
      operation_already_in_progress,
      operation_not_supported,
      operation_not_supported_on_transport_endpoint,
      operation_now_in_progress,
      overlapped_IO_event_object_not_in_signaled_state,
      overlapped_operation_aborted,
      overlapped_operations_will_complete_later,
      permission_denied,
      protocol_family_not_supported,
      protocol_not_available,
      protocol_wrong_type_for_socket,
      socket_is_already_connected,
      socket_is_not_connected,
      socket_operation_on_nonsocket,
      socket_type_not_supported,
      software_caused_connection_abort,
      specified_event_object_handle_is_invalid,
      system_call_failure,
      this_is_a_nonrecoverable_error,
      too_many_open_files,
      too_many_processes,
      unknown_protocol,
      valid_name_no_data_record_of_requested_type,
      last_error_no=valid_name_no_data_record_of_requested_type,

      system_specific_error=-4, //< not one of the above
//       WouldBlockWrite=-3
//       WouldBlockRead=-2,
      WouldBlock=-1,
      Success=0
    };

    BOOST_STATIC_ASSERT(last_error_no<system_specific_error);

  }// namespace
}// namespace

#endif

