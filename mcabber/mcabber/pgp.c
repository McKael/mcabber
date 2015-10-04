/*
 * pgp.c        -- PGP utility functions
 *
 * Copyright (C) 2006-2015 Mikael Berthe <mikael@lilotux.net>
 * Some parts inspired by centericq (impgp.cc)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include <config.h>

#ifdef HAVE_GPGME

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <locale.h>
#include <sys/mman.h>
#include <glib.h>

#include "pgp.h"
#include "logprint.h"

#define MIN_GPGME_VERSION "1.0.0"

static struct gpg_struct
{
  int enabled;
  char *private_key;
  char *passphrase;
} gpg;


//  gpg_init(priv_key, passphrase)
// Initialize the GPG sub-systems.  This function must be invoked early.
// Note: priv_key & passphrase are optional, they can be set later.
// This function returns 0 if gpgme is available and initialized;
// if not it returns the gpgme error code.
int gpg_init(const char *priv_key, const char *passphrase)
{
  gpgme_error_t err;

  // Check for version and OpenPGP protocol support.
  if (!gpgme_check_version(MIN_GPGME_VERSION)) {
    scr_LogPrint(LPRINT_LOGNORM,
                 "GPGME initialization error: Bad library version");
    return -1;
  }

  err = gpgme_engine_check_version(GPGME_PROTOCOL_OpenPGP);
  if (err) {
    scr_LogPrint(LPRINT_LOGNORM|LPRINT_NOTUTF8,
                 "GPGME initialization error: %s", gpgme_strerror(err));
    return err;
  }

  // Set the locale information.
  gpgme_set_locale(NULL, LC_CTYPE, setlocale(LC_CTYPE, NULL));
  gpgme_set_locale(NULL, LC_MESSAGES, setlocale(LC_MESSAGES, NULL));

  // Store private data.
  gpg_set_private_key(priv_key);
  gpg_set_passphrase(passphrase);

  gpg.enabled = 1;
  return 0;
}

//  gpg_terminate()
// Destroy data and free memory.
void gpg_terminate(void)
{
  gpg.enabled = 0;
  gpg_set_passphrase(NULL);
  gpg_set_private_key(NULL);
}

//  gpg_set_passphrase(passphrase)
// Set the current passphrase (use NULL to erase it).
void gpg_set_passphrase(const char *passphrase)
{
  // Remove current passphrase
  if (gpg.passphrase) {
    ssize_t len = strlen(gpg.passphrase);
    memset(gpg.passphrase, 0, len);
    munlock(gpg.passphrase, len);
    g_free(gpg.passphrase);
  }
  if (passphrase) {
    gpg.passphrase = g_strdup(passphrase);
    mlock(gpg.passphrase, strlen(gpg.passphrase));
  } else {
    gpg.passphrase = NULL;
  }
}

//  gpg_set_private_key(keyid)
// Set the current private key id (use NULL to unset it).
void gpg_set_private_key(const char *priv_keyid)
{
  g_free(gpg.private_key);
  if (priv_keyid)
    gpg.private_key = g_strdup(priv_keyid);
  else
    gpg.private_key = NULL;
}

//  gpg_get_private_key_id()
// Return the current private key id (static string).
const char *gpg_get_private_key_id(void)
{
  return gpg.private_key;
}

//  strip_header_footer(data)
// Remove PGP header & footer from data.
// Return a new string, or NULL.
// The string must be freed by the caller with g_free() when no longer needed.
static char *strip_header_footer(const char *data)
{
  char *p, *q;

  if (!data)
    return NULL;

  // p: beginning of real data
  // q: end of real data

  // Strip header (to the first empty line)
  p = strstr(data, "\n\n");
  if (!p)
    return g_strdup(data);

  // Strip footer
  // We want to remove the last lines, until the line beginning with a '-'
  p += 2;
  for (q = p ; *q; q++) ;
  // (q is at the end of data now)
  for (q--; q > p && (*q != '\n' || *(q+1) != '-'); q--) ;

  if (q <= p)
    return NULL; // Shouldn't happen...

  return g_strndup(p, q-p);
}

// GCC ignores casts to void, thus we need to hack around that
static inline void ignore(void*x) {}

//  passphrase_cb()
// GPGME passphrase callback function.
static gpgme_error_t passphrase_cb(void *hook, const char *uid_hint,
                       const char *passphrase_info, int prev_was_bad, int fd)
{
  ssize_t len;

  // Abort if we do not have the password.
  if (!gpg.passphrase) {
    ignore((void*)write(fd, "\n", 1)); // We have an error anyway, thus it does
                                       // not matter if we fail again.
    return gpg_error(GPG_ERR_CANCELED);
  }

  // Write the passphrase to the file descriptor.
  len = strlen(gpg.passphrase);
  if (write(fd, gpg.passphrase, len) != len)
    return gpg_error(GPG_ERR_CANCELED);
  if (write(fd, "\n", 1) != 1)
    return gpg_error(GPG_ERR_CANCELED);

  return 0; // Success
}

//  gpg_verify(gpg_data, text, *sigsum)
// Verify that gpg_data is a correct signature for text.
// Return the key id (or fingerprint), and set *sigsum to
// the gpgme signature summary value.
// The returned string must be freed with g_free() after use.
char *gpg_verify(const char *gpg_data, const char *text,
                 gpgme_sigsum_t *sigsum)
{
  gpgme_ctx_t ctx;
  gpgme_data_t data_sign, data_text;
  char *data;
  char *verified_key = NULL;
  gpgme_key_t key;
  gpgme_error_t err;
  const char prefix[] = "-----BEGIN PGP SIGNATURE-----\n\n";
  const char suffix[] = "\n-----END PGP SIGNATURE-----\n";

  // Reset the summary.
  *sigsum = 0;

  if (!gpg.enabled)
    return NULL;

  err = gpgme_new(&ctx);
  if (err) {
    scr_LogPrint(LPRINT_LOGNORM|LPRINT_NOTUTF8,
                 "GPGME error: %s", gpgme_strerror(err));
    return NULL;
  }

  gpgme_set_protocol(ctx, GPGME_PROTOCOL_OpenPGP);

  // Surround the given data with the prefix & suffix
  data = g_new(char, sizeof(prefix) + sizeof(suffix) + strlen(gpg_data));
  strcpy(data, prefix);
  strcat(data, gpg_data);
  strcat(data, suffix);

  err = gpgme_data_new_from_mem(&data_sign, data, strlen(data), 0);
  if (!err) {
    err = gpgme_data_new_from_mem(&data_text, text, strlen(text), 0);
    if (!err) {
      err = gpgme_op_verify(ctx, data_sign, data_text, 0);
      if (!err) {
        gpgme_verify_result_t vr = gpgme_op_verify_result(ctx);
        if (vr && vr->signatures) {
          char *r = vr->signatures->fpr;
          // Found the fingerprint.  Let's try to get the key id.
          if (!gpgme_get_key(ctx, r, &key, 0) && key) {
            r = key->subkeys->keyid;
            gpgme_key_release(key);
          }
          // r is a static variable, let's copy it.
          verified_key = g_strdup(r);
          *sigsum = vr->signatures->summary;
          // For some reason summary could be 0 when status is 0 too,
          // which means the signature is valid...
          if (!*sigsum && !vr->signatures->status)
            *sigsum = GPGME_SIGSUM_GREEN;
        }
      }
      gpgme_data_release(data_text);
    }
    gpgme_data_release(data_sign);
  }
  if (err)
    scr_LogPrint(LPRINT_LOGNORM|LPRINT_NOTUTF8,
                 "GPGME verification error: %s", gpgme_strerror(err));
  gpgme_release(ctx);
  g_free(data);
  return verified_key;
}

//  gpg_sign(gpg_data)
// Return a signature of gpg_data (or NULL).
// The returned string must be freed with g_free() after use.
char *gpg_sign(const char *gpg_data)
{
  gpgme_ctx_t ctx;
  gpgme_data_t in, out;
  char *p;
  char *signed_data = NULL;
  size_t nread;
  gpgme_key_t key;
  gpgme_error_t err;

  if (!gpg.enabled || !gpg.private_key)
    return NULL;

  err = gpgme_new(&ctx);
  if (err) {
    scr_LogPrint(LPRINT_LOGNORM|LPRINT_NOTUTF8,
                 "GPGME error: %s", gpgme_strerror(err));
    return NULL;
  }

  gpgme_set_protocol(ctx, GPGME_PROTOCOL_OpenPGP);
  gpgme_set_textmode(ctx, 0);
  gpgme_set_armor(ctx, 1);

  p = getenv("GPG_AGENT_INFO");
  if (!(p && strchr(p, ':')))
    gpgme_set_passphrase_cb(ctx, passphrase_cb, 0);

  err = gpgme_get_key(ctx, gpg.private_key, &key, 1);
  if (err || !key) {
    scr_LogPrint(LPRINT_LOGNORM, "GPGME error: private key not found");
    gpgme_release(ctx);
    return NULL;
  }

  gpgme_signers_clear(ctx);
  gpgme_signers_add(ctx, key);
  gpgme_key_release(key);
  err = gpgme_data_new_from_mem(&in, gpg_data, strlen(gpg_data), 0);
  if (!err) {
    err = gpgme_data_new(&out);
    if (!err) {
      err = gpgme_op_sign(ctx, in, out, GPGME_SIG_MODE_DETACH);
      if (!err) {
        signed_data = gpgme_data_release_and_get_mem(out, &nread);
        if (signed_data) {
          // We need to add a trailing NULL
          char *dd = g_strndup(signed_data, nread);
          free(signed_data);
          signed_data = strip_header_footer(dd);
          g_free(dd);
        }
      } else {
        gpgme_data_release(out);
      }
    }
    gpgme_data_release(in);
  }
  if (err && err != GPG_ERR_CANCELED)
    scr_LogPrint(LPRINT_LOGNORM|LPRINT_NOTUTF8,
                 "GPGME signature error: %s", gpgme_strerror(err));
  gpgme_release(ctx);
  return signed_data;
}

//  gpg_decrypt(gpg_data)
// Return decrypted gpg_data (or NULL).
// The returned string must be freed with g_free() after use.
char *gpg_decrypt(const char *gpg_data)
{
  gpgme_ctx_t ctx;
  gpgme_data_t in, out;
  char *p, *data;
  char *decrypted_data = NULL;
  size_t nread;
  gpgme_error_t err;
  const char prefix[] = "-----BEGIN PGP MESSAGE-----\n\n";
  const char suffix[] = "\n-----END PGP MESSAGE-----\n";

  if (!gpg.enabled)
    return NULL;

  err = gpgme_new(&ctx);
  if (err) {
    scr_LogPrint(LPRINT_LOGNORM|LPRINT_NOTUTF8,
                 "GPGME error: %s", gpgme_strerror(err));
    return NULL;
  }

  gpgme_set_protocol(ctx, GPGME_PROTOCOL_OpenPGP);

  p = getenv("GPG_AGENT_INFO");
  if (!(p && strchr(p, ':')))
    gpgme_set_passphrase_cb(ctx, passphrase_cb, 0);

  // Surround the given data with the prefix & suffix
  data = g_new(char, sizeof(prefix) + sizeof(suffix) + strlen(gpg_data));
  strcpy(data, prefix);
  strcat(data, gpg_data);
  strcat(data, suffix);

  err = gpgme_data_new_from_mem(&in, data, strlen(data), 0);
  if (!err) {
    err = gpgme_data_new(&out);
    if (!err) {
      err = gpgme_op_decrypt(ctx, in, out);
      if (!err) {
        decrypted_data = gpgme_data_release_and_get_mem(out, &nread);
        if (decrypted_data) {
          // We need to add a trailing NULL
          char *dd = g_strndup(decrypted_data, nread);
          free(decrypted_data);
          decrypted_data = dd;
        }
      } else {
        gpgme_data_release(out);
      }
    }
    gpgme_data_release(in);
  }
  if (err && err != GPG_ERR_CANCELED)
    scr_LogPrint(LPRINT_LOGNORM|LPRINT_NOTUTF8,
                 "GPGME decryption error: %s", gpgme_strerror(err));
  gpgme_release(ctx);
  g_free(data);
  return decrypted_data;
}

//  gpg_encrypt(gpg_data, keyids[], n)
// Return encrypted gpg_data with the n keys from the keyids array (or NULL).
// The returned string must be freed with g_free() after use.
char *gpg_encrypt(const char *gpg_data, const char *keyids[], size_t nkeys)
{
  gpgme_ctx_t ctx;
  gpgme_data_t in, out;
  char *encrypted_data = NULL, *edata;
  size_t nread;
  gpgme_key_t *keys;
  gpgme_error_t err;
  unsigned i;

  if (!gpg.enabled)
    return NULL;

  if (!keyids || !nkeys) {
    return NULL;
  }

  err = gpgme_new(&ctx);
  if (err) {
    scr_LogPrint(LPRINT_LOGNORM|LPRINT_NOTUTF8,
                 "GPGME error: %s", gpgme_strerror(err));
    return NULL;
  }

  gpgme_set_protocol(ctx, GPGME_PROTOCOL_OpenPGP);
  gpgme_set_textmode(ctx, 0);
  gpgme_set_armor(ctx, 1);

  keys = g_new0(gpgme_key_t, 1+nkeys);

  for (i = 0; i < nkeys; i++) {
    err = gpgme_get_key(ctx, keyids[i], &keys[i], 0);
    if (err || !keys[i]) {
      scr_LogPrint(LPRINT_LOGNORM, "GPGME encryption error: cannot use key %s",
                   keyids[i]);
      // We need to have err not null to ensure we won't try to encrypt
      // without this key.
      if (!err) err = GPG_ERR_UNKNOWN_ERRNO;
      break;
    }
  }

  if (!err) {
    err = gpgme_data_new_from_mem(&in, gpg_data, strlen(gpg_data), 0);
    if (!err) {
      err = gpgme_data_new(&out);
      if (!err) {
        err = gpgme_op_encrypt(ctx, keys, GPGME_ENCRYPT_ALWAYS_TRUST, in, out);
        if (!err)
          encrypted_data = gpgme_data_release_and_get_mem(out, &nread);
        else
          gpgme_data_release(out);
      }
      gpgme_data_release(in);
    }

    if (err && err != GPG_ERR_CANCELED) {
      scr_LogPrint(LPRINT_LOGNORM|LPRINT_NOTUTF8,
                   "GPGME encryption error: %s", gpgme_strerror(err));
    }
  }

  for (i = 0; keys[i]; i++)
    gpgme_key_release(keys[i]);
  g_free(keys);
  gpgme_release(ctx);
  edata = strip_header_footer(encrypted_data);
  if (encrypted_data)
    free(encrypted_data);
  return edata;
}

//  gpg_test_passphrase()
// Test the current gpg.passphrase with gpg.private_key.
// If the test doesn't succeed, the passphrase is cleared and a non-null
// value is returned.
int gpg_test_passphrase(void)
{
  char *s;

  if (!gpg.private_key)
    return -1; // No private key...

  s = gpg_sign("test");
  if (s) {
    free(s);
    return 0; // Ok, test successful
  }
  // The passphrase is wrong (if provided)
  gpg_set_passphrase(NULL);
  return -1;
}

int gpg_enabled(void)
{
  return gpg.enabled;
}

#else  /* not HAVE_GPGME */

int gpg_enabled(void)
{
  return 0;
}

#endif /* HAVE_GPGME */

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
