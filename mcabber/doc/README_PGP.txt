 MCabber OpenPGP support

This files explains how to use PGP support in mcabber.

Please note that you need libgpgme > 1.0.0 (for example, libgpgme11 in Debian)
on your system.


## Enabling OpenPGP support ##

In the configuration file, enable pgp and set pgp_private_key to your key id.
Your key id can be found with the following command:
  % gpg --list-keys --keyid-format long your_name

Example (in $HOME/.mcabber/mcabberrc):

set pgp = 1
set pgp_private_key = "E3E6A9C1A6A013D3"


## Encrypting messages ##

Now when you start mcabber, it should ask for your passphrase (unless you put
it in your configuration file or you use gpg-agent).

If you want to know if a contact is using PGP, select the contact and use the
command /info. If (s)he is, it should display something like:

    PGP key id: E2C4C9A1601A5A4
    Last PGP signature: unknown

The signature is "unknown", because we don't have the contact's key. We could
get it with gpg, for example:
  % gpg --recv-keys E2C4C9A1601A5A4

Then, wait for the next presence message.

If the contact has your key and you have their key, you should have
bidirectional encrypted messages.


## Per-contact settings ##

You can provide a PGP key to be used for a given Jabber user or disable PGP on
a per-account basis, using the command /pgp.

If you provide a KeyId for a contact, it will be compared to the key the
contact uses to sign their presence/messages and it will be used for all
outgoing encrypted messages (by default, mcabber will use the contact
signature's key).

Example:
 /pgp disable foo@bar.org
    (disables encryption of messages sent to foo@bar.org)
 /pgp setkey bar@foo.net C9940A9BB0B92210
    (set the encryption key for bar@foo.net and warn if this contact doesn't
     use this key for their signatures)
 /pgp info
    (show the PGP settings we've set for the currently selected contact)

Try "/help pgp" for a usage description.

The command /pgp can be used in the configuration file (without the leading /).
