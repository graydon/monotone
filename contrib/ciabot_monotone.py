#!/usr/bin/env python
#
# Copyright (C) Nathaniel Smith <njs@pobox.com>
# Licensed under the MIT license:
#   http://www.opensource.org/licenses/mit-license.html
# I.e., do what you like, but keep copyright and there's NO WARRANTY.
#
# CIA bot client script for Monotone repositories, written in python.  This
# generates commit messages using CIA's XML commit format, and can deliver
# them using either XML-RPC or email.  Based on the script 'ciabot_svn.py' by
# Micah Dowty <micah@navi.cx>.

# This script is normally run from a cron job.  It periodically does a 'pull'
# from a given server, finds new revisions, filters them for "interesting"
# ones, and reports them to CIA.

# It needs a working directory, where it will store the database and some
# state of its own.

# To use:
#   -- make a copy of it somewhere
#   -- edit the configuration values below
#   -- set up a cron job to run every ten minutes (or whatever), running the
#      command "ciabot_monotone.py <path to scratch dir>".  The scratch dir is
#      used to store state between runs.  It will be automatically created,
#      but do not delete it.

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

    # Add entries of the form ("server address", "pattern") to get
    # this script to watch the given collections at the given monotone
    # servers.
    watch_list = [
        #("monotone.ca", "net.venge.monotone"),
        ]

    # If this is non-None, then the web interface will make any file 'foo' a
    # link to 'repository_uri/foo'.
    repository_uri = None

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

    # Path to monotone executable.
    monotone_exec = "monotone"

################################################################################

import sys, os, os.path

class Monotone:
    def __init__(self, bin, db):
        self.bin = bin
        self.db = db

    def _run_monotone(self, args):
        args_str = " ".join(args)
        # Yay lack of quoting
        fd = os.popen("%s --db=%s --quiet %s" % (self.bin, self.db, args_str))
        output = fd.read()
        if fd.close():
            sys.exit("monotone exited with error")
        return output

    def _split_revs(self, output):
        if output:
            return output.strip().split("\n")
        else:
            return []

    def get_interface_version(self):
        iv_str = self._run_monotone(["automate", "interface_version"])
        return tuple(map(int, iv_str.strip().split(".")))

    def db_init(self):
        self._run_monotone(["db", "init"])

    def db_migrate(self):
        self._run_monotone(["db", "migrate"])

    def ensure_db_exists(self):
        if not os.path.exists(self.db):
            self.db_init()

    def pull(self, server, collection):
        self._run_monotone(["pull", server, collection])

    def leaves(self):
        return self._split_revs(self._run_monotone(["automate", "leaves"]))

    def ancestry_difference(self, new_rev, old_revs):
        args = ["automate", "ancestry_difference", new_rev] + old_revs
        return self._split_revs(self._run_monotone(args))

    def log(self, rev, xlast=None):
        if xlast is not None:
            last_arg = ["--last=%i" % (xlast,)]
        else:
            last_arg = []
        return self._run_monotone(["log", "-r", rev] + last_arg)

    def toposort(self, revs):
        args = ["automate", "toposort"] + revs
        return self._split_revs(self._run_monotone(args))

    def get_revision(self, rid):
        return self._run_monotone(["automate", "get_revision", rid])

class LeafFile:
    def __init__(self, path):
        self.path = path

    def get_leaves(self):
        if os.path.exists(self.path):
            f = open(self.path, "r")
            lines = []
            for line in f:
                lines.append(line.strip())
            return lines
        else:
            return []

    def set_leaves(self, leaves):
        f = open(self.path + ".new", "w")
        for leaf in leaves:
            f.write(leaf + "\n")
        f.close()
        os.rename(self.path + ".new", self.path)

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

def send_change_for(rid, m, c):
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

    log = m.log(rid, 1)
    rev = m.get_revision(rid)
    # Stupid way to pull out everything inside quotes (which currently
    # uniquely identifies filenames inside a changeset).
    pieces = rev.split('"')
    files = []
    for i in range(len(pieces)):
        if (i % 2) == 1:
            if pieces[i] not in files:
                files.append(pieces[i])
    substs["files"] = "\n".join(["<file>%s</file>" % escape_for_xml(f) for f in files])
    branch = None
    author = None
    changelog_pieces = []
    started_changelog = 0
    pieces = log.split("\n")
    for p in pieces:
        if p.startswith("Author:"):
            author = p.split(None, 1)[1].strip()
        if p.startswith("Branch:"):
            branch = p.split()[1]
        if p.startswith("ChangeLog:"):
            started_changelog = 1
        elif started_changelog:
            changelog_pieces.append(p)
    changelog = "\n".join(changelog_pieces).strip()
    if branch is None:
        return
    project = c.project_for_branch(branch)
    if project is None:
        return
    substs["author"] = escape_for_xml(author or "(unknown author)")
    substs["project"] = escape_for_xml(project)
    substs["branch"] = escape_for_xml(branch)
    substs["rid"] = escape_for_xml(rid)
    substs["log"] = escape_for_xml(changelog)

    message = message_tmpl % substs
    send_message(message, c)

def send_changes_between(old_leaves, new_leaves, m, c):
    if not old_leaves:
        # Special case for initial setup -- don't push thousands of old
        # revisions down CIA's throat!
        return
    new_revs = {}
    for leaf in new_leaves:
        if leaf in old_leaves:
            continue
        for new_rev in m.ancestry_difference(leaf, old_leaves):
            new_revs[new_rev] = None
    new_revs_sorted = m.toposort(new_revs.keys())
    for new_rev in new_revs_sorted:
        send_change_for(new_rev, m, c)

def main(progname, args):
    if len(args) != 1:
        sys.exit("Usage: %s STATE-DIR" % (progname,))
    (state_dir,) = args
    if not os.path.isdir(state_dir):
        os.makedirs(state_dir)
    lockfile = os.path.join(state_dir, "lock")
    # Small race condition, oh well.
    if os.path.exists(lockfile):
        sys.exit("script already running, exiting")
    try:
        open(lockfile, "w").close()
        c = config()
        m = Monotone(c.monotone_exec, os.path.join(state_dir, "database.db"))
        m.ensure_db_exists()
        m.db_migrate()
        for server, collection in c.watch_list:
            m.pull(server, collection)
        lf = LeafFile(os.path.join(state_dir, "leaves"))
        new_leaves = m.leaves()
        send_changes_between(lf.get_leaves(), new_leaves, m, c)
        lf.set_leaves(new_leaves)
    finally:
        os.unlink(lockfile)

if __name__ == "__main__":
    main(sys.argv[0], sys.argv[1:])
