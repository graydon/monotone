#!/usr/bin/env python
#
# Copyright (C) Nathaniel Smith <njs@pobox.com>
#               Timothy Brownawell <tbrownaw@gmail.com>
#               Thomas Moschny <thomas.moschny@gmx.de>
# Licensed under the MIT license:
#   http://www.opensource.org/licenses/mit-license.html
# I.e., do what you like, but keep copyright and there's NO WARRANTY.
#
# CIA bot client script for Monotone repositories, written in python.  This
# generates commit messages using CIA's XML commit format, and can deliver
# them using either XML-RPC or email.  Based on the script 'ciabot_svn.py' by
# Micah Dowty <micah@navi.cx>.

# This version is modified to be called by a server hook, instead of a cron job.

# To use:
#   -- make a copy of it somewhere
#   -- edit the configuration values below
#   -- include ciabot_monotone_hookversion.lua in the server's monotonerc:

class config:
    def project_for_branch(self, branchname):
        # Customize this to return your project name(s).  If changes to the
        # given branch are uninteresting -- i.e., changes to them should be
        # ignored entirely -- then return the python constant None (which is
        # distinct from the string "None", a valid but poor project name!).
        #if branchname.startswith("net.venge.monotone-viz"):
        #    return "monotone-viz"
        #elif branchname.startswith("net.venge.monotone.contrib.monotree"):
        #    return "monotree"
        #elif branchname.startswith("net.venge.monotone"):
        #    return "monotone"
        return "FIXME"

    # The server to deliver XML-RPC messages to, if using XML-RPC delivery.
    xmlrpc_server = "http://cia.navi.cx"

    # The email address to deliver messages to, if using email delivery.
    smtp_address = "cia@cia.navi.cx"

    # The SMTP server to connect to, if using email delivery.
    smtp_server = "localhost"

    # The 'from' address to put on email, if using email delivery.
    from_address = "cia-user@FIXME"

    # Set to one of "xmlrpc", "email", "debug".
    delivery = "debug"

################################################################################

import sys
import re
import os

def escape_for_xml(text, is_attrib=0):
    text = text.replace("&", "&amp;")
    text = text.replace("<", "&lt;")
    text = text.replace(">", "&gt;")
    if is_attrib:
        text = text.replace("'", "&apos;")
        text = text.replace("\"", "&quot;")
    return text

TOKEN = re.compile(r'''
    "(?P<str>(\\\\|\\"|[^"])*)"
    |\[(?P<id>[a-f0-9]{40}|)\]
    |(?P<key>\w+)
    |(?P<ws>\s+)
''', re.VERBOSE)

def parse_basic_io(raw):
    parsed = []
    key = None
    for m in TOKEN.finditer(raw):
        if m.lastgroup == 'key':
            if key:
                parsed.append((key, values))
            key = m.group('key')
            values = []
        elif m.lastgroup == 'id':
            values.append(m.group('id'))
        elif m.lastgroup == 'str':
            value = m.group('str')
            # dequote: replace \" with "
            value = re.sub(r'\\"', '"', value)
            # dequote: replace \\ with \
            value = re.sub(r'\\\\', r'\\', value)
            values.append(value)
    if key:
        parsed.append((key, values))
    return parsed

def send_message(message, c):
    if c.delivery == "debug":
        print message
    elif c.delivery == "xmlrpc":
        import xmlrpclib
        xmlrpclib.ServerProxy(c.xmlrpc_server).hub.deliver(message)
    elif c.delivery == "email":
        import smtplib
        smtp = smtplib.SMTP(c.smtp_server)
        smtp.sendmail(c.from_address, c.smtp_address,
                      "From: %s\r\nTo: %s\r\n"
                      "Subject: DeliverXML\r\n\r\n%s"
                      % (c.from_address, c.smtp_address, message))
    else:
        sys.exit("delivery option must be one of 'debug', 'xmlrpc', 'email'")

def send_change_for(rid, branch, author, log, rev, c):
    message_tmpl = """<message>
    <generator>
        <name>Monotone CIA Bot client python script</name>
        <version>0.1</version>
    </generator>
    <source>
        <project>%(project)s</project>
        <branch>%(branch)s</branch>
    </source>
    <body>
        <commit>
            <revision>%(rid)s</revision>
            <author>%(author)s</author>
            <files>%(files)s</files>
            <log>%(log)s</log>
        </commit>
    </body>
</message>"""
    
    substs = {}
    files = []
    for key, values in parse_basic_io(rev):
        if key == 'old_revision':
            # start a new changeset
            oldpath = None
        if key == 'delete':
            files.append('<file action="remove">%s</file>'
                         % escape_for_xml(values[0]))
        elif key == 'rename':
            oldpath = values[0]
        elif key == 'to':
            if oldpath:
                files.append('<file action="rename" to="%s">%s</file>'
                             % (escape_for_xml(values[0]), escape_for_xml(oldpath)))
                oldpath = None
        elif key == 'add_dir':
            files.append('<file action="add">%s</file>'
                         % escape_for_xml(values[0] + '/'))
        elif key == 'add_file':
            files.append('<file action="add">%s</file>'
                          % escape_for_xml(values[0]))
        elif key == 'patch':
            files.append('<file action="modify">%s</file>'
                         % escape_for_xml(values[0]))
            
    substs["files"] = "\n".join(files)
    changelog = log.strip()
    project = c.project_for_branch(branch)
    if project is None:
        return
    substs["author"] = escape_for_xml(author)
    substs["project"] = escape_for_xml(project)
    substs["branch"] = escape_for_xml(branch)
    substs["rid"] = escape_for_xml(rid)
    substs["log"] = escape_for_xml(changelog)

    message = message_tmpl % substs
    send_message(message, c)

def main(progname, args):
    if len(args) != 5:
        sys.exit("Usage: %s revid branch author changelog revision_text" % (progname, ))
    # We don't want to clutter the process table with zombies; but we also
    # don't want to force the monotone server to wait around while we call the
    # CIA server.  So we fork -- the original process exits immediately, and
    # the child continues (orphaned, so it will eventually be reaped by init).
    if hasattr(os, "fork"):
        if os.fork():
            return
    (rid, branch, author, log, rev, ) = args
    c = config()
    send_change_for(rid, branch, author, log, rev, c)

if __name__ == "__main__":
    main(sys.argv[0], sys.argv[1:])
