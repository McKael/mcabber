#!/usr/bin/env python
# -*- coding: utf-8 -*-
# 
# Copyright (C) 2013 Sharoon Thomas <sharoon.thomas@openlabs.co.in>
#
# This script is provided under the terms of the GNU General Public License,
# see the file COPYING in the root mcabber source directory.
#
#
# This script displays notifications using the new notification center
# introduced in OSX 10.8.
import sys
import os

try:
    from pync import Notifier
except ImportError, exc:
    print "TIP: Use 'pip install pync'"
    raise exc


def handle_msg(type_, source, filename=None):
    """
    Handle a message

    :param type_: IN, OUT or MUC (Multi User Chat)
    :type type_: string
    :param source: Jabber ID or Nickname, or Room ID for MUC
    :param filename: Filename of message body if event_log_files was set
    """
    if type_ == "IN":
        if filename:
            Notifier.notify(
                open(filename).read(),
                title=source, group='mcabber',
            )
        else:
            Notifier.notify(
                "Sent you a message",
                title=source, group='mcabber',
            )
    if filename and os.path.exists(filename):
        os.remove(filename)


def parse(event, *args):
    """
    Parses the arguments received and calls the appropriate function

    MSG IN jabber@id [file] (when receiving a message)
    MSG OUT jabber@id       (when sending a message)
    MSG MUC room_id [file]  (when receiving a MUC message)
    STATUS X jabber@id      (new buddy status is X)
    UNREAD "N x y z"        (number of unread buddy buffers)

    :param event: Type of event "MSG", "STATUS", "UNREAD"
    :param args: tuple of arguments
    """
    if event == "MSG":
        handle_msg(*args)

if __name__ == "__main__":
    parse(*sys.argv[1:])

