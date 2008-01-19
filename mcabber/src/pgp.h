#ifndef __PGP_H__
#define __PGP_H__ 1

#include <config.h>

#ifdef HAVE_GPGME

#define GPGME_ERR_SOURCE_DEFAULT GPG_ERR_SOURCE_USER_1
#include <gpgme.h>

int   gpg_init(const char *priv_key, const char *passphrase);
void  gpg_terminate(void);
void  gpg_set_passphrase(const char *passphrase);
void  gpg_set_private_key(const char *priv_keyid);
char *gpg_verify(const char *gpg_data, const char *text,
                 gpgme_sigsum_t *sigsum);
char *gpg_sign(const char *gpg_data);
char *gpg_decrypt(const char *gpg_data);
char *gpg_encrypt(const char *gpg_data, const char *keyid);

int   gpg_test_passphrase(void);

#endif /* HAVE_GPGME */

int gpg_enabled(void);

#endif /* __PGP_H__ */

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
