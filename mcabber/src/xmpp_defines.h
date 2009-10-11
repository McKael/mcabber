#ifndef __XMPP_DEFINES_H__
#define __XMPP_DEFINES_H__ 1

#define NS_CLIENT    "jabber:client"
#define NS_SERVER    "jabber:server"
#define NS_DIALBACK  "jabber:server:dialback"
#define NS_AUTH      "jabber:iq:auth"
#define NS_AUTH_CRYPT "jabber:iq:auth:crypt"
#define NS_REGISTER  "jabber:iq:register"
#define NS_ROSTER    "jabber:iq:roster"
#define NS_OFFLINE   "jabber:x:offline"
#define NS_AGENT     "jabber:iq:agent"
#define NS_AGENTS    "jabber:iq:agents"
#define NS_DELAY     "jabber:x:delay"
#define NS_VERSION   "jabber:iq:version"
#define NS_TIME      "jabber:iq:time"
#define NS_VCARD     "vcard-temp"
#define NS_PRIVATE   "jabber:iq:private"
#define NS_SEARCH    "jabber:iq:search"
#define NS_OOB       "jabber:iq:oob"
#define NS_XOOB      "jabber:x:oob"
#define NS_ADMIN     "jabber:iq:admin"
#define NS_FILTER    "jabber:iq:filter"
#define NS_AUTH_0K   "jabber:iq:auth:0k"
#define NS_BROWSE    "jabber:iq:browse"
#define NS_EVENT     "jabber:x:event"
#define NS_CONFERENCE "jabber:iq:conference"
#define NS_SIGNED    "jabber:x:signed"
#define NS_ENCRYPTED "jabber:x:encrypted"
#define NS_GATEWAY   "jabber:iq:gateway"
#define NS_LAST      "jabber:iq:last"
#define NS_ENVELOPE  "jabber:x:envelope"
#define NS_EXPIRE    "jabber:x:expire"
#define NS_XHTML     "http://www.w3.org/1999/xhtml"
#define NS_DISCO_INFO "http://jabber.org/protocol/disco#info"
#define NS_DISCO_ITEMS "http://jabber.org/protocol/disco#items"
#define NS_IQ_AUTH    "http://jabber.org/features/iq-auth"
#define NS_REGISTER_FEATURE "http://jabber.org/features/iq-register"

#define NS_CAPS       "http://jabber.org/protocol/caps"
#define NS_CHATSTATES "http://jabber.org/protocol/chatstates"
#define NS_COMMANDS   "http://jabber.org/protocol/commands"
#define NS_MUC        "http://jabber.org/protocol/muc"

#define NS_XDBGINSERT "jabber:xdb:ginsert"
#define NS_XDBNSLIST  "jabber:xdb:nslist"

#define NS_XMPP_STANZAS "urn:ietf:params:xml:ns:xmpp-stanzas"
#define NS_XMPP_TLS  "urn:ietf:params:xml:ns:xmpp-tls"
#define NS_XMPP_STREAMS "urn:ietf:params:xml:ns:xmpp-streams"

#define NS_XMPP_DELAY "urn:xmpp:delay"
#define NS_XMPP_TIME  "urn:xmpp:time"
#define NS_PING       "urn:xmpp:ping"

#define NS_JABBERD_STOREDPRESENCE "http://jabberd.org/ns/storedpresence"
#define NS_JABBERD_HISTORY "http://jabberd.org/ns/history"

#define XMPP_ERROR_REDIRECT              302
#define XMPP_ERROR_BAD_REQUEST           400
#define XMPP_ERROR_NOT_AUTHORIZED        401
#define XMPP_ERROR_PAYMENT_REQUIRED      402
#define XMPP_ERROR_FORBIDDEN             403
#define XMPP_ERROR_NOT_FOUND             404
#define XMPP_ERROR_NOT_ALLOWED           405
#define XMPP_ERROR_NOT_ACCEPTABLE        406
#define XMPP_ERROR_REGISTRATION_REQUIRED 407
#define XMPP_ERROR_REQUEST_TIMEOUT       408
#define XMPP_ERROR_CONFLICT              409
#define XMPP_ERROR_INTERNAL_SERVER_ERROR 500
#define XMPP_ERROR_NOT_IMPLEMENTED       501
#define XMPP_ERROR_REMOTE_SERVER_ERROR   502
#define XMPP_ERROR_SERVICE_UNAVAILABLE   503
#define XMPP_ERROR_REMOTE_SERVER_TIMEOUT 504
#define XMPP_ERROR_DISCONNECTED          510

#endif
