#!/usr/bin/env python
#
# Copyright (C) Nathaniel Smith <njs@pobox.com>
#               Timothy Brownawell <tbrownaw@gmail.com>
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
#   -- put the following in the server's monotonerc:
# function note_netsync_revision_received(rid, rdat, certs)
#    local branch, author, changelog
#    for i, cert in pairs(certs)
#    do
#       if (cert.name == "branch") then
#          branch = cert.value
#       end
#       if (cert.name == "author") then
#          author = cert.value
#       end
#       if (cert.name == "changelog") then
#          changelog = cert.value
#       end
#    end
#    local exe = "/path/to/this/script"
#    spawn(exe, rid, branch, author, changelog, rdat)
#    return
# end

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
        #else:
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

def escape_for_xml(text, is_attrib=0):
    text = text.replace("&", "&amp;")
    text = text.replace("<", "&lt;")
    text = text.replace(">", "&gt;")
    if is_attrib:
        text = text.replace("'", "&apos;")
        text = text.replace("\"", "&quot;")
    return text

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
    
    # Stupid way to pull out everything inside quotes (which currently
    # uniquely identifies filenames inside a changeset).
    pieces = rev.split('"')
    files = []
    for i in range(len(pieces)):
        if (i % 2) == 1:
            if pieces[i] not in files:
                files.append(pieces[i])
    substs["files"] = "\n".join(["<file>%s</file>" % escape_for_xml(f) for f in files])
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
    (rid, branch, author, log, rev, ) = args
    c = config()
    send_change_for(rid, branch, author, log, rev, c)

if __name__ == "__main__":
    main(sys.argv[0], sys.argv[1:])
